#include "http_connection.hpp"
#include <spdlog/spdlog.h>
#include <iostream>

HttpConnection::HttpConnection(tcp::socket socket, std::shared_ptr<Router> router)
    : socket_(std::move(socket)), router_(std::move(router)) {}

void HttpConnection::start() {
    read_request();
}

void HttpConnection::read_request() {
    auto self = shared_from_this();
    http::async_read(
        socket_,
        buffer_,
        request_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_read(ec, bytes_transferred);
        });
}

void HttpConnection::on_read(beast::error_code ec, std::size_t /*bytes_transferred*/) {
    if (ec) {
        if (ec != http::error::end_of_stream) {
            spdlog::warn("read error: {}", ec.message());
        }
        return;
    }

    std::cout << "===== REQUEST =====\n"
              << request_
              << "\n====================\n";

    build_response();
    write_response();
}

void HttpConnection::build_response() {
    response_.version(request_.version());
    response_.set(http::field::server, "api-gateway");
    response_.set(http::field::content_type, "text/plain");

    auto matched = router_->match(request_.method(), request_.target());

    if (matched.has_value()) {
        response_.result(http::status::ok);
        response_.body() = "Routed to service: " + matched->service_name + "\n";
        spdlog::info("{} {} -> {}",
                     std::string(request_.method_string()),
                     std::string(request_.target()),
                     matched->service_name);
    } else {
        response_.result(http::status::not_found);
        response_.body() = "404 Not Found: no route matches this request\n";
        spdlog::info("{} {} -> 404 (no matching route)",
                     std::string(request_.method_string()),
                     std::string(request_.target()));
    }

    response_.prepare_payload();
}

void HttpConnection::write_response() {

    std::cout << "===== RESPONSE =====\n"
              << response_
              << "\n=====================\n";

    auto self = shared_from_this();
    http::async_write(
        socket_,
        response_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(ec, bytes_transferred);
        });
}

void HttpConnection::on_write(beast::error_code ec, std::size_t /*bytes_transferred*/) {
    if (ec) {
        spdlog::warn("write error: {}", ec.message());
        return;
    }

    beast::error_code ignored;
    socket_.shutdown(tcp::socket::shutdown_send, ignored);
}