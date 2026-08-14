#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

// Forwards ONE request to ONE backend and reports back the response (or an
// error). Supports two modes:
//   1. Cold path: DNS resolve → TCP connect → write → read (no pooled socket)
//   2. Warm path: write → read (using a pooled, already-connected socket)
//
// Same enable_shared_from_this pattern as HttpConnection, for the same reason:
// async operations outlive the function call that started them, so this object
// must stay alive until they finish.
class HttpClient : public std::enable_shared_from_this<HttpClient> {
public:
    using ResponseHandler = std::function<void(beast::error_code)>;

    explicit HttpClient(net::io_context& ioc);

    // Cold path -- no pooled connection available, must resolve + connect.
    void async_forward(const std::string& host,
                        unsigned short port,
                        http::request<http::string_body>& request,
                        http::response<http::string_body>& response,
                        std::chrono::milliseconds timeout,
                        ResponseHandler handler);

    // Warm path -- reuse an already-connected socket from the pool.
    void async_forward(beast::tcp_stream pooled_stream,
                        http::request<http::string_body>& request,
                        http::response<http::string_body>& response,
                        std::chrono::milliseconds timeout,
                        ResponseHandler handler);

    // After a successful response, the caller can reclaim the stream to
    // return it to the connection pool.
    beast::tcp_stream release_stream();

private:
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type endpoint);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);

    // Returns a reference to the active stream (asserts it exists).
    beast::tcp_stream& stream();

    net::strand<net::io_context::executor_type> strand_;
    tcp::resolver resolver_;
    // Wrapped in optional because beast::tcp_stream has its move-assignment
    // operator deleted. The warm path needs to replace the stream with a
    // pooled one, which requires destroy + emplace (move-construct).
    std::optional<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body>* request_ptr_ = nullptr;
    http::response<http::string_body>* response_ptr_ = nullptr;
    ResponseHandler handler_;
};