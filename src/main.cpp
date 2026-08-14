#include "http_connection.hpp"
#include "configuration.hpp"
#include "health_checker.hpp"
#include "connection_pool.hpp"
#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <thread>
#include <vector>

namespace net = boost::asio;
using tcp = net::ip::tcp;

void accept_loop(tcp::acceptor& acceptor,
                  net::io_context& ioc,
                  std::shared_ptr<Router> router,
                  std::shared_ptr<ServiceRegistry> registry,
                  std::shared_ptr<ConnectionPool> pool) {
    acceptor.async_accept(
        [&acceptor, &ioc, router, registry, pool](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<HttpConnection>(
                    std::move(socket), router, registry, pool, ioc
                )->start();
            } else {
                spdlog::warn("accept error: {}", ec.message());
            }
            accept_loop(acceptor, ioc, router, registry, pool);
        });
}

int main() {
    spdlog::info("api-gateway starting up (with backend connection pooling)");

    Configuration config;
    try {
        config = Configuration::load("config/gateway.yaml");
    } catch (const std::exception& e) {
        spdlog::error("failed to load configuration: {}", e.what());
        return 1; // refuse to start on bad config -- fail fast
    }

    const unsigned short port = 8080;
    net::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), port));
    spdlog::info("listening on port {}", port);

    // Create the global backend connection pool.
    auto pool = std::make_shared<ConnectionPool>(ioc, config.pool_config);
    pool->start_eviction_timer();

    accept_loop(acceptor, ioc, config.router, config.registry, pool);

    auto health_checker = std::make_shared<HealthChecker>(ioc, config.registry, config.health_check, pool);
    health_checker->start();

    const unsigned int thread_count = std::max(2u, std::thread::hardware_concurrency());
    spdlog::info("using {} threads", thread_count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count - 1);
    for (unsigned int i = 0; i < thread_count - 1; ++i) {
        threads.emplace_back([&ioc] { ioc.run(); });
    }
    ioc.run();
    for (auto& t : threads) t.join();

    return 0;
}