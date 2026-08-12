#include "http_connection.hpp"
#include <spdlog/spdlog.h>

HttpConnection::HttpConnection(tcp::socket socket)
    : socket_(std::move(socket)) {}

void HttpConnection::start() {
    read_request();
}

void HttpConnection::read_request() {
    // async_read_until Beast's HTTP parser understands the framing itself,
    // so we just say "read into buffer_, parse into request_, call on_read
    // when a full request has arrived (or an error happens)."
    //
    // shared_from_this() is handed to the lambda so this HttpConnection
    // object stays alive until on_read actually fires — even though
    // read_request() itself returns immediately.
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
        // A real client disconnecting mid-read is normal and expected —
        // don't treat it as a scary error, just stop.
        if (ec != http::error::end_of_stream) {
            spdlog::warn("read error: {}", ec.message());
        }
        return;
    }

    build_response();
    write_response();
}

void HttpConnection::build_response() {
    // Phase 2 goal: prove the request/response cycle works end to end.
    // Every request gets the same fixed response for now — real routing
    // logic comes in Phase 3.
    response_.version(request_.version());
    response_.result(http::status::ok);
    response_.set(http::field::server, "api-gateway");
    response_.set(http::field::content_type, "text/plain");
    response_.body() = "Hello from the gateway (Phase 2 skeleton)\n";
    response_.prepare_payload(); // fills in Content-Length correctly
}

void HttpConnection::write_response() {
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

    // Cleanly shut down our side of the socket, then let the shared_ptr
    // chain end — once nothing references this HttpConnection anymore,
    // it's destroyed automatically. No manual "delete" anywhere.
    beast::error_code ignored;
    socket_.shutdown(tcp::socket::shutdown_send, ignored);
}