#include "http_connection.hpp"
#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <thread>
#include <vector>

namespace net = boost::asio;
using tcp = net::ip::tcp;

void accept_loop(tcp::acceptor& acceptor) {
    // async_accept: wait for a client, hand us a ready-to-use socket for
    // that specific client, then immediately go wait for the NEXT client
    // (that's the recursive call to accept_loop at the bottom).
    acceptor.async_accept(
        [&acceptor](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                // Each accepted connection gets its own HttpConnection,
                // owned by a shared_ptr, and we kick off its read/write chain.
                std::make_shared<HttpConnection>(std::move(socket))->start();
            } else {
                spdlog::warn("accept error: {}", ec.message());
            }

            // Keep accepting new connections indefinitely.
            accept_loop(acceptor);
        });
}

int main() {
    spdlog::info("api-gateway starting up (Phase 2: HTTP server)");

    const unsigned short port = 8080;
    net::io_context ioc;

    tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), port));
    spdlog::info("listening on port {}", port);

    accept_loop(acceptor);

    // Run the event loop on a small pool of threads instead of just one.
    // This is what lets Asio actually use multiple CPU cores: any thread
    // in this pool can pick up any ready callback.
    const unsigned int thread_count = std::max(2u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    threads.reserve(thread_count - 1);

    for (unsigned int i = 0; i < thread_count - 1; ++i) {
        threads.emplace_back([&ioc] { ioc.run(); });
    }

    ioc.run(); // main thread also participates in running the event loop

    for (auto& t : threads) {
        t.join();
    }

    return 0;
}