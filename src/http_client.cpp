#include "http_client.hpp"
#include <spdlog/spdlog.h>

HttpClient::HttpClient(net::io_context& ioc)
    : strand_(net::make_strand(ioc)),
      resolver_(strand_),
      stream_(beast::tcp_stream(strand_)) {}

beast::tcp_stream& HttpClient::stream() {
    return *stream_;
}

// Cold path: resolve → connect → write → read
void HttpClient::async_forward(const std::string& host,
                                unsigned short port,
                                http::request<http::string_body>& request,
                                http::response<http::string_body>& response,
                                std::chrono::milliseconds timeout,
                                ResponseHandler handler) {
    buffer_.consume(buffer_.size());
    request_ptr_ = &request;
    response_ptr_ = &response;
    handler_ = std::move(handler);

    stream().expires_after(timeout);

    auto self = shared_from_this();
    resolver_.async_resolve(
        host, std::to_string(port),
        [self](beast::error_code ec, tcp::resolver::results_type results) {
            self->on_resolve(ec, results);
        });
}

// Warm path: skip resolve + connect, jump straight to writing the request
// on an already-connected pooled socket.
void HttpClient::async_forward(beast::tcp_stream pooled_stream,
                                http::request<http::string_body>& request,
                                http::response<http::string_body>& response,
                                std::chrono::milliseconds timeout,
                                ResponseHandler handler) {
    // beast::tcp_stream has deleted move-assignment, so we destroy the
    // default-constructed stream and emplace the pooled one in its place.
    stream_.emplace(std::move(pooled_stream));
    buffer_.consume(buffer_.size());
    request_ptr_ = &request;
    response_ptr_ = &response;
    handler_ = std::move(handler);

    stream().expires_after(timeout);

    // Go directly to writing -- the socket is already connected.
    auto self = shared_from_this();
    http::async_write(
        stream(), *request_ptr_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(ec, bytes_transferred);
        });
}

beast::tcp_stream HttpClient::release_stream() {
    return std::move(*stream_);
}

void HttpClient::on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec) { auto h = std::move(handler_); h(ec); return; }

    auto self = shared_from_this();
    stream().async_connect(
        results,
        [self](beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
            self->on_connect(ec, ep);
        });
}

void HttpClient::on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
    if (ec) {
        auto h = std::move(handler_);
        h(ec);
        return;
    }

    beast::error_code option_ec;
    stream().socket().set_option(tcp::no_delay(true), option_ec);
    if (option_ec) {
        spdlog::warn("failed to set TCP_NODELAY on backend socket: {}", option_ec.message());
    }

    auto self = shared_from_this();
    http::async_write(
        stream(), *request_ptr_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(ec, bytes_transferred);
        });
}

void HttpClient::on_write(beast::error_code ec, std::size_t) {
    if (ec) { auto h = std::move(handler_); h(ec); return; }

    auto self = shared_from_this();
    http::async_read(
        stream(), buffer_, *response_ptr_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_read(ec, bytes_transferred);
        });
}

void HttpClient::on_read(beast::error_code ec, std::size_t) {
    // Move the handler out before calling it -- this breaks the circular
    // reference (handler_ holds a lambda that captures shared_ptr<HttpClient>
    // pointing back at *this). Without this move, neither the HttpClient nor
    // the ConnectionGuard captured in the lambda would ever be freed.
    auto handler = std::move(handler_);
    if (ec) { handler(ec); return; }
    handler({});
}