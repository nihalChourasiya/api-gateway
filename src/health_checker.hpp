#pragma once

#include "service_registry.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <memory>

namespace net = boost::asio;
namespace beast = boost::beast;

struct HealthCheckConfig {
    std::chrono::milliseconds interval{5000};
    std::chrono::milliseconds timeout{1000};
    int unhealthy_after = 3;   // consecutive failures before marking down
    int healthy_after = 1;     // consecutive successes before marking up
    std::string path = "/";    // what to probe -- "/" works with any backend
};

class HealthChecker : public std::enable_shared_from_this<HealthChecker> {
public:
    HealthChecker(net::io_context& ioc,
                  std::shared_ptr<ServiceRegistry> registry,
                  HealthCheckConfig config);

    void start();

private:
    void schedule_tick();
    void on_tick(beast::error_code ec);
    void check_once();
    void probe_instance(BackendInstance& instance);
    void handle_probe_result(BackendInstance& instance, bool success);

    net::io_context& ioc_;
    std::shared_ptr<ServiceRegistry> registry_;
    HealthCheckConfig config_;
    net::steady_timer timer_;
};