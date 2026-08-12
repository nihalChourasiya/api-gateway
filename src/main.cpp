#include "http_connection.hpp"
#include "router.hpp"
#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <thread>
#include <vector>

namespace net = boost::asio;
using tcp = net::ip::tcp;

std::shared_ptr<Router> build_router() {
    auto router = std::make_shared<Router>();

    // Hardcoded for now -- this becomes YAML-driven in Phase 5.
    router->add_route({http::verb::get, "/users", MatchType::Prefix, "user-service"});
    router->add_route({http::verb::get, "/orders", MatchType::Prefix, "order-service"});
    router->add_route({http::verb::get, "/health", MatchType::Exact, "gateway-self"});

    return router;
}

void accept_loop(tcp::acceptor& acceptor, std::shared_ptr<Router> router) {
    acceptor.async_accept(
        [&acceptor, router](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<HttpConnection>(std::move(socket), router)->start();
            } else {
                spdlog::warn("accept error: {}", ec.message());
            }

            accept_loop(acceptor, router);
        });
}

int main() {
    spdlog::info("api-gateway starting up (Phase 3: request routing)");

    const unsigned short port = 8080;
    net::io_context ioc;

    tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), port));
    spdlog::info("listening on port {}", port);

    auto router = build_router();
    accept_loop(acceptor, router);

    const unsigned int thread_count = std::max(2u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    threads.reserve(thread_count - 1);

    for (unsigned int i = 0; i < thread_count - 1; ++i) {
        threads.emplace_back([&ioc] { ioc.run(); });
    }

    ioc.run();

    for (auto& t : threads) {
        t.join();
    }

    return 0;
}