// A minimal, fully async HTTP backend used ONLY for load-testing the
// gateway -- not part of the gateway itself. Same accept/read/write
// pattern as Phase 2's HttpConnection, stripped down to always return "ok".
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket) : socket_(std::move(socket)) {
        beast::error_code ec;
        socket_.set_option(tcp::no_delay(true), ec);
    }
    void start() { read(); }

private:
    void read() {
        auto self = shared_from_this();
        http::async_read(socket_, buffer_, request_,
            [self](beast::error_code ec, std::size_t) {
                if (ec) return;
                self->respond();
            });
    }

    void respond() {
        response_.version(request_.version());
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "text/plain");
    
        // 1. Tell the client whether we are keeping the connection alive
        response_.keep_alive(request_.keep_alive()); 
    
        response_.body() = "ok";
        response_.prepare_payload();

        auto self = shared_from_this();
        http::async_write(socket_, response_,
            [self](beast::error_code ec, std::size_t) {
            if (ec) return;

            // 2. Check if the connection should be closed based on the headers/version
            if (self->response_.need_eof()) {
                beast::error_code ignored;
                self->socket_.shutdown(tcp::socket::shutdown_both, ignored);
                return;
            }

            // 3. Keep-Alive: Clear the previous request and loop back to read()
            self->request_ = {};
            self->response_ = {};
            self->read();
        });
    }

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    http::response<http::string_body> response_;
};

void accept_loop(tcp::acceptor& acceptor) {
    acceptor.async_accept([&acceptor](beast::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<Session>(std::move(socket))->start();
        }
        accept_loop(acceptor);
    });
}

int main(int argc, char* argv[]) {
    unsigned short port = argc > 1 ? static_cast<unsigned short>(std::atoi(argv[1])) : 9001;

    net::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), port));
    std::cout << "mock backend listening on " << port << "\n";

    accept_loop(acceptor);

    unsigned int threads = std::max(2u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned int i = 0; i < threads - 1; ++i) pool.emplace_back([&ioc] { ioc.run(); });
    ioc.run();
    for (auto& t : pool) t.join();
}