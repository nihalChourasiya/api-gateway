#include "http_connection.hpp"
#include "http_client.hpp"
#include <spdlog/spdlog.h>
#include "connection_guard.hpp"

HttpConnection::HttpConnection(tcp::socket socket,
                                std::shared_ptr<Router> router,
                                std::shared_ptr<ServiceRegistry> registry,
                                net::io_context& ioc)
    : socket_(std::move(socket)),
      router_(std::move(router)),
      registry_(std::move(registry)),
      ioc_(ioc) {}

void HttpConnection::start() {
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

    if (!matched_route_.has_value()) {
        spdlog::info("{} {} -> 404", std::string(request_.method_string()), std::string(request_.target()));
        send_error(http::status::not_found, "404 Not Found: no route matches this request\n");
        return;
    }

    spdlog::info("{} {} -> {} (forwarding)",
                 std::string(request_.method_string()),
                 std::string(request_.target()),
                 matched_route_->service_name);
    forward_request();
}

#include "connection_guard.hpp"
// (add this #include near the top, alongside the others)

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

    spdlog::info("selected backend {}:{} for {} (active_connections now {})",
                 instance->host, instance->port, matched_route_->service_name,
                 instance->active_connections.load() + 1);

    request_.erase(http::field::connection);
    request_.erase(http::field::keep_alive);
    request_.erase(http::field::proxy_authenticate);
    request_.erase(http::field::proxy_authorization);
    request_.erase(http::field::te);
    request_.erase(http::field::trailer);
    request_.erase(http::field::transfer_encoding);
    request_.erase(http::field::upgrade);

    request_.set(http::field::host, instance->host + ":" + std::to_string(instance->port));

    beast::error_code ep_ec;
    auto remote = socket_.remote_endpoint(ep_ec);
    if (!ep_ec) {
        request_.set("X-Forwarded-For", remote.address().to_string());
    }

    request_.keep_alive(false);

    // The guard is heap-allocated and captured BY the completion lambda,
    // not held as a local stack variable -- this is the important part.
    // forward_request() returns almost immediately (async_forward just
    // schedules work and comes back), so a stack-local guard would be
    // destroyed (decrementing the counter) way too early, before the
    // backend has even responded. Capturing a shared_ptr to the guard in
    // the lambda keeps it alive for exactly as long as this request is
    // actually in flight, and it's destroyed automatically the instant
    // the lambda finishes running -- covering every exit path uniformly.
    auto guard = std::make_shared<ConnectionGuard>(*instance);

    auto client = std::make_shared<HttpClient>(ioc_);
    auto self = shared_from_this();

    client->async_forward(
        instance->host, instance->port, request_, std::chrono::milliseconds(3000),
        [self, guard](beast::error_code ec, http::response<http::string_body> backend_response) {
            self->on_backend_response(ec, std::move(backend_response));
        });
}

void HttpConnection::send_error(http::status status, const std::string& message) {
    response_.version(request_.version());
    response_.result(status);
    response_.set(http::field::server, "api-gateway");
    response_.set(http::field::content_type, "text/plain");
    response_.body() = message;
    response_.prepare_payload();
    write_response();
}

void HttpConnection::on_backend_response(beast::error_code ec,
                                          http::response<http::string_body> backend_response) {
    if (ec) {
        if (ec == net::error::timed_out) {
            spdlog::warn("backend timeout: {}", ec.message());
            send_error(http::status::gateway_timeout, "504 Gateway Timeout: backend did not respond in time\n");
        } else {
            spdlog::warn("backend connection error: {}", ec.message());
            send_error(http::status::bad_gateway, "502 Bad Gateway: could not reach backend (" + ec.message() + ")\n");
        }
        return;
    }

    response_ = std::move(backend_response);
    response_.set(http::field::server, "api-gateway");
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
    beast::error_code ignored;
    socket_.shutdown(tcp::socket::shutdown_send, ignored);
}