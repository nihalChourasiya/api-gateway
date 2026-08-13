#include "http_client.hpp"

HttpClient::HttpClient(net::io_context& ioc)
    : resolver_(net::make_strand(ioc)), stream_(net::make_strand(ioc)) {}

void HttpClient::async_forward(const std::string& host,
                                unsigned short port,
                                http::request<http::string_body> request,
                                std::chrono::milliseconds timeout,
                                ResponseHandler handler) {
    request_ = std::move(request);
    handler_ = std::move(handler);

    stream_.expires_after(timeout);

    auto self = shared_from_this();
    resolver_.async_resolve(
        host, std::to_string(port),
        [self](beast::error_code ec, tcp::resolver::results_type results) {
            self->on_resolve(ec, results);
        });
}

void HttpClient::on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec) { handler_(ec, {}); return; }

    auto self = shared_from_this();
    stream_.async_connect(
        results,
        [self](beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
            self->on_connect(ec, ep);
        });
}

void HttpClient::on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
    if (ec) { handler_(ec, {}); return; }

    auto self = shared_from_this();
    http::async_write(
        stream_, request_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(ec, bytes_transferred);
        });
}

void HttpClient::on_write(beast::error_code ec, std::size_t) {
    if (ec) { handler_(ec, {}); return; }

    auto self = shared_from_this();
    http::async_read(
        stream_, buffer_, response_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_read(ec, bytes_transferred);
        });
}

void HttpClient::on_read(beast::error_code ec, std::size_t) {
    beast::error_code ignored;
    stream_.socket().shutdown(tcp::socket::shutdown_both, ignored);

    if (ec) { handler_(ec, {}); return; }
    handler_({}, std::move(response_));
}