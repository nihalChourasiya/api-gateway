#include "http_connection.hpp"
#include "http_client.hpp"
#include <spdlog/spdlog.h>
#include "connection_guard.hpp"

HttpConnection::HttpConnection(tcp::socket socket,
                                std::shared_ptr<Router> router,
                                std::shared_ptr<ServiceRegistry> registry,
                                std::shared_ptr<ConnectionPool> pool,
                                net::io_context& ioc)
    : socket_(std::move(socket)),
      router_(std::move(router)),
      registry_(std::move(registry)),
      pool_(std::move(pool)),
      ioc_(ioc),
      client_(std::make_shared<HttpClient>(ioc)) {
    
    beast::error_code ec;
    socket_.set_option(tcp::no_delay(true), ec);
    if (ec) {
        spdlog::warn("failed to set TCP_NODELAY on client socket: {}", ec.message());
    }
}

void HttpConnection::start() {
    beast::error_code ep_ec;
    auto remote = socket_.remote_endpoint(ep_ec);
    if (!ep_ec) {
        client_ip_ = remote.address().to_string();
    }
    read_request();
}

void HttpConnection::read_request() {
    auto self = shared_from_this();
    http::async_read(
        socket_, buffer_, request_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_read(ec, bytes_transferred);
        });
}

void HttpConnection::on_read(beast::error_code ec, std::size_t) {
    if (ec) {
        if (ec != http::error::end_of_stream) {
            spdlog::warn("read error: {}", ec.message());
        }
        return;
    }
    handle_request();
}

void HttpConnection::handle_request() {
    matched_route_ = router_->match(request_.method(), request_.target());

    if (matched_route_ == nullptr) {
        spdlog::info("{} {} -> 404", std::string(request_.method_string()), std::string(request_.target()));
        send_error(http::status::not_found, "404 Not Found: no route matches this request\n");
        return;
    }

    forward_request();
}

void HttpConnection::forward_request() {
    Service* service = registry_->find(matched_route_->service_name);

    if (service == nullptr || service->instances.empty()) {
        send_error(http::status::service_unavailable,
                   "503 Service Unavailable: no backend instances configured for '"
                       + matched_route_->service_name + "'\n");
        return;
    }

    BackendInstance* instance = service->balancer->select(service->instances);

    if (instance == nullptr) {
        send_error(http::status::service_unavailable,
                   "503 Service Unavailable: load balancer returned no instance\n");
        return;
    }

    // Strip hop-by-hop headers before forwarding
    request_.erase(http::field::connection);
    request_.erase(http::field::keep_alive);
    request_.erase(http::field::proxy_authenticate);
    request_.erase(http::field::proxy_authorization);
    request_.erase(http::field::te);
    request_.erase(http::field::trailer);
    request_.erase(http::field::transfer_encoding);
    request_.erase(http::field::upgrade);

    request_.set(http::field::host, instance->host_header);

    if (!client_ip_.empty()) {
        request_.set("X-Forwarded-For", client_ip_);
    }

    // Ask the backend to keep the connection alive so we can pool it.
    request_.keep_alive(true);

    // The guard is managed as a member of HttpConnection. It lives exactly
    // as long as the backend request is in flight, and is reset in on_backend_response.
    guard_.emplace(*instance);

    // Capture backend instance ID so we can return the socket to the pool later.
    backend_instance_id_ = instance->id;

    auto self = shared_from_this();

    // Try the warm path first: grab an already-connected socket from the pool.
    auto pooled = pool_->acquire(backend_instance_id_);

    if (pooled.has_value()) {
        // Warm path -- skip DNS resolve and TCP connect entirely.
        client_->async_forward(
            std::move(*pooled), request_, response_, std::chrono::milliseconds(3000),
            [self](beast::error_code ec) {
                self->on_backend_response(ec);
            });
    } else {
        // Cold path -- open a brand new connection.
        client_->async_forward(
            instance->host, instance->port, request_, response_, std::chrono::milliseconds(3000),
            [self](beast::error_code ec) {
                self->on_backend_response(ec);
            });
    }
}

void HttpConnection::send_error(http::status status, const std::string& message) {
    response_ = {};
    response_.version(request_.version() == 0 ? 11 : request_.version());
    response_.result(status);
    response_.set(http::field::server, "api-gateway");
    response_.set(http::field::content_type, "text/plain");
    response_.body() = message;
    response_.prepare_payload();
    write_response();
}

void HttpConnection::on_backend_response(beast::error_code ec) {
    if (ec) {
        if (ec == net::error::timed_out) {
            spdlog::warn("backend timeout: {}", ec.message());
            send_error(http::status::gateway_timeout, "504 Gateway Timeout: backend did not respond in time\n");
        } else {
            spdlog::warn("backend connection error: {}", ec.message());
            send_error(http::status::bad_gateway, "502 Bad Gateway: could not reach backend (" + ec.message() + ")\n");
        }
        // On error, do NOT return the socket to the pool -- let it close.
        guard_.reset();
        return;
    }

    // Defer the pool release until on_write -- the response hasn't been
    // fully sent to the client yet (write_response is async), so we can't
    // move the backend stream into the pool here.
    pending_release_ = response_.keep_alive();
    guard_.reset();

    response_.set(http::field::server, "api-gateway");

    // Mirror the client's keep-alive preference in the response we send back.
    response_.keep_alive(request_.keep_alive());

    write_response();
}

void HttpConnection::write_response() {
    auto self = shared_from_this();
    http::async_write(
        socket_, response_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(ec, bytes_transferred);
        });
}

void HttpConnection::on_write(beast::error_code ec, std::size_t) {
    if (ec) { spdlog::warn("write error: {}", ec.message()); return; }

    // Now that the response is fully written to the client, it's safe to
    // return the backend socket to the pool.
    if (pending_release_) {
        pool_->release(backend_instance_id_, client_->release_stream());
        pending_release_ = false;
    }

    guard_.reset(); // Safety reset in case send_error was called

    // Respect the client's keep-alive preference: if the client wants to
    // reuse this connection (HTTP/1.1 default), loop back and read the
    // next request instead of shutting down.
    if (response_.keep_alive()) {
        request_ = {};
        response_ = {};
        matched_route_ = nullptr;
        buffer_.consume(buffer_.size());

        read_request();
    } else {
        beast::error_code ignored;
        socket_.shutdown(tcp::socket::shutdown_send, ignored);
    }
}