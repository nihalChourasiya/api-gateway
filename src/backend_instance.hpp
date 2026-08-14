#pragma once

#include <atomic>
#include <string>

struct BackendInstance {
    std::string host;
    unsigned short port;
    std::string host_header;
    int id = -1; // Unique ID for zero-allocation connection pooling
    std::atomic<int> active_connections{0};

    // Written ONLY by HealthChecker (effectively single-threaded, since it
    // always runs through the same repeating timer callback). Read by every
    // request thread via LoadBalancer::select() -- hence atomic.
    std::atomic<bool> is_healthy{true};

    // Private bookkeeping for HealthChecker's hysteresis logic. NOT atomic,
    // on purpose: only HealthChecker's own callback chain ever touches these,
    // so there's no concurrent access to protect against. Don't be tempted
    // to "atomic everything" here -- it would just be needless overhead for
    // state nothing else ever reads.
    int consecutive_failures = 0;
    int consecutive_successes = 0;

    BackendInstance(std::string h, unsigned short p, int instance_id = -1)
        : host(std::move(h)), port(p), host_header(host + ":" + std::to_string(port)), id(instance_id) {}

    BackendInstance(BackendInstance&& other) noexcept
        : host(std::move(other.host)),
          port(other.port),
          host_header(std::move(other.host_header)),
          id(other.id),
          active_connections(other.active_connections.load()),
          is_healthy(other.is_healthy.load()),
          consecutive_failures(other.consecutive_failures),
          consecutive_successes(other.consecutive_successes) {}

    BackendInstance(const BackendInstance&) = delete;
    BackendInstance& operator=(const BackendInstance&) = delete;
    BackendInstance& operator=(BackendInstance&&) = delete;
};