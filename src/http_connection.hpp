#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>
#include <optional>
#include "router.hpp"
#include "service_registry.hpp"

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    HttpConnection(tcp::socket socket,
                   std::shared_ptr<Router> router,
                   std::shared_ptr<ServiceRegistry> registry,
                   net::io_context& ioc);

    void start();

private:
    void read_request();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void handle_request();
    void forward_request();
    void send_error(http::status status, const std::string& message);
    void on_backend_response(beast::error_code ec, http::response<http::string_body> backend_response);
    void write_response();
    void on_write(beast::error_code ec, std::size_t bytes_transferred);

    tcp::socket socket_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<ServiceRegistry> registry_;
    net::io_context& ioc_;

    beast::flat_buffer buffer_{8192};
    http::request<http::string_body> request_;
    http::response<http::string_body> response_;
    std::optional<Route> matched_route_;
};