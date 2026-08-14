#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace net = boost::asio;
namespace beast = boost::beast;
using tcp = net::ip::tcp;

struct PooledConnection {
    std::unique_ptr<beast::tcp_stream> stream;
    std::chrono::steady_clock::time_point idle_since;

    PooledConnection(beast::tcp_stream s)
        : stream(std::make_unique<beast::tcp_stream>(std::move(s))),
          idle_since(std::chrono::steady_clock::now()) {}
};

struct PoolConfig {
    int max_idle_per_backend = 8;
    int idle_timeout_ms = 30000;
    int total_instances = 0;
};

// Global connection pool keyed by "host:port". Thread-safe -- all public
// methods lock internally, since io_context::run() may be called from
// multiple threads.
class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
public:
    ConnectionPool(net::io_context& ioc, PoolConfig config);

    // Try to grab an already-connected socket for this backend.
    // Returns nullopt if the pool for this backend is empty.
    std::optional<beast::tcp_stream> acquire(int instance_id);

    // Return a still-healthy socket to the pool after use.
    // Drops it silently if the pool for this backend is already full.
    void release(int instance_id, beast::tcp_stream stream);

    // Immediately close and discard all pooled connections for a backend.
    // Called when HealthChecker marks a backend unhealthy.
    void drain(int instance_id);

    // Start the periodic idle-connection eviction timer.
    void start_eviction_timer();

private:
    void evict_expired();
    void schedule_eviction();

    net::io_context& ioc_;
    PoolConfig config_;
    net::steady_timer eviction_timer_;

    struct PoolSlot {
        std::mutex mutex;
        std::vector<PooledConnection> connections;
    };
    std::vector<std::unique_ptr<PoolSlot>> pools_;
};
