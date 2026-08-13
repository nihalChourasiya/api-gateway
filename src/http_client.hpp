#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

// Forwards ONE request to ONE backend and reports back the response (or an
// error). One HttpClient = one outgoing request -- no pooling/reuse yet
// (that's a Phase 13 improvement). Same enable_shared_from_this pattern as
// HttpConnection, for the same reason: async operations outlive the function
// call that started them, so this object must stay alive until they finish.
class HttpClient : public std::enable_shared_from_this<HttpClient> {
public:
    using ResponseHandler =
        std::function<void(beast::error_code, http::response<http::string_body>)>;

    explicit HttpClient(net::io_context& ioc);

    void async_forward(const std::string& host,
                        unsigned short port,
                        http::request<http::string_body> request,
                        std::chrono::milliseconds timeout,
                        ResponseHandler handler);

private:
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type endpoint);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);

    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    http::response<http::string_body> response_;
    ResponseHandler handler_;
};