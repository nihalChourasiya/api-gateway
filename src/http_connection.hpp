#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

// Handles the full lifecycle of ONE client connection: read the request,
// build a response, write it back, then close (or read the next request,
// for keep-alive — we'll add that once the basics work).
//
// Inherits from enable_shared_from_this so it can safely hand out shared_ptrs
// to itself inside async callbacks
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    explicit HttpConnection(tcp::socket socket);

    // Kicks off the read -> handle -> write chain. Called once, right after
    // the connection is accepted.
    void start();

private:
    void read_request();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void build_response();
    void write_response();
    void on_write(beast::error_code ec, std::size_t bytes_transferred);

    tcp::socket socket_;

    // Beast needs a buffer to accumulate bytes into while it incrementally
    // parses the HTTP request off the wire. 8KB is a generous starting size
    // for a request with no large body.
    beast::flat_buffer buffer_{8192};

    http::request<http::string_body> request_;
    http::response<http::string_body> response_;
};