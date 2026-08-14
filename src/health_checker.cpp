#include "health_checker.hpp"
#include "http_client.hpp"
#include <spdlog/spdlog.h>

HealthChecker::HealthChecker(net::io_context& ioc,
                              std::shared_ptr<ServiceRegistry> registry,
                              HealthCheckConfig config,
                              std::shared_ptr<ConnectionPool> pool)
    : ioc_(ioc),
      registry_(std::move(registry)),
      config_(config),
      pool_(std::move(pool)),
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

struct ProbeContext {
    std::shared_ptr<HttpClient> client;
    http::request<http::string_body> req;
    http::response<http::string_body> res;
};

void HealthChecker::probe_instance(BackendInstance& instance) {
    auto ctx = std::make_shared<ProbeContext>();
    ctx->client = std::make_shared<HttpClient>(ioc_);
    ctx->req.method(http::verb::get);
    ctx->req.target(config_.path);
    ctx->req.version(11);
    ctx->req.set(http::field::host, instance.host + ":" + std::to_string(instance.port));
    ctx->req.set(http::field::user_agent, "api-gateway-health-checker");

    auto self = shared_from_this();

    ctx->client->async_forward(
        instance.host, instance.port, ctx->req, ctx->res, config_.timeout,
        [self, &instance, ctx](beast::error_code ec) {
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

            // Drain all pooled connections to this backend immediately --
            // no point sending future requests on sockets to a dead server.
            pool_->drain(instance.id);
        }
    }
}