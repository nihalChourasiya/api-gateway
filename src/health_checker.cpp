#include "health_checker.hpp"
#include "http_client.hpp"
#include <spdlog/spdlog.h>

HealthChecker::HealthChecker(net::io_context& ioc,
                              std::shared_ptr<ServiceRegistry> registry,
                              HealthCheckConfig config)
    : ioc_(ioc),
      registry_(std::move(registry)),
      config_(config),
      timer_(ioc) {}

void HealthChecker::start() {
    spdlog::info("health checker starting: interval={}ms, timeout={}ms, unhealthy_after={}, healthy_after={}",
                 config_.interval.count(), config_.timeout.count(),
                 config_.unhealthy_after, config_.healthy_after);
    schedule_tick();
}

void HealthChecker::schedule_tick() {
    timer_.expires_after(config_.interval);

    auto self = shared_from_this();
    timer_.async_wait([self](beast::error_code ec) {
        self->on_tick(ec);
    });
}

void HealthChecker::on_tick(beast::error_code ec) {
    if (ec) {
        // Timer was cancelled (e.g. during shutdown, in a later phase) --
        // stop rescheduling rather than treating this as a real error.
        return;
    }

    check_once();

    // Reschedule immediately -- we don't wait for this round's probes to
    // finish. Each probe has its own timeout, independent of tick interval.
    schedule_tick();
}

void HealthChecker::check_once() {
    for (auto& [name, service] : registry_->all_services()) {
        for (auto& instance : service.instances) {
            probe_instance(instance);
        }
    }
}

void HealthChecker::probe_instance(BackendInstance& instance) {
    http::request<http::string_body> request{http::verb::get, config_.path, 11};
    request.set(http::field::host, instance.host + ":" + std::to_string(instance.port));
    request.set(http::field::user_agent, "api-gateway-health-checker");

    auto client = std::make_shared<HttpClient>(ioc_);
    auto self = shared_from_this();

    client->async_forward(
        instance.host, instance.port, request, config_.timeout,
        [self, &instance](beast::error_code ec, http::response<http::string_body> /*response*/) {
            // Success here means "got ANY HTTP response before the timeout" --
            // we deliberately don't check the status code. A 404 still proves
            // the instance is alive, reachable, and speaking HTTP correctly,
            // which is all a liveness probe needs to establish.
            bool success = !ec;
            self->handle_probe_result(instance, success);
        });
}

void HealthChecker::handle_probe_result(BackendInstance& instance, bool success) {
    if (success) {
        instance.consecutive_failures = 0;
        instance.consecutive_successes++;

        if (!instance.is_healthy.load() && instance.consecutive_successes >= config_.healthy_after) {
            instance.is_healthy.store(true);
            spdlog::info("backend {}:{} is now HEALTHY (recovered)", instance.host, instance.port);
        }
    } else {
        instance.consecutive_successes = 0;
        instance.consecutive_failures++;

        if (instance.is_healthy.load() && instance.consecutive_failures >= config_.unhealthy_after) {
            instance.is_healthy.store(false);
            spdlog::warn("backend {}:{} is now UNHEALTHY ({} consecutive failures)",
                         instance.host, instance.port, instance.consecutive_failures);
        }
    }
}