#include "connection_pool.hpp"
#include <spdlog/spdlog.h>

ConnectionPool::ConnectionPool(net::io_context& ioc, PoolConfig config)
    : ioc_(ioc),
      config_(config),
      eviction_timer_(ioc) {
    if (config_.total_instances > 0) {
        pools_.reserve(config_.total_instances);
        for (int i = 0; i < config_.total_instances; ++i) {
            pools_.push_back(std::make_unique<PoolSlot>());
        }
    }
}

std::optional<beast::tcp_stream> ConnectionPool::acquire(int instance_id) {
    if (instance_id < 0 || instance_id >= static_cast<int>(pools_.size())) return std::nullopt;

    auto& slot = pools_[instance_id];
    if (!slot) return std::nullopt;

    std::lock_guard<std::mutex> lock(slot->mutex);
    auto& lst = slot->connections;
    if (lst.empty()) {
        return std::nullopt;
    }

    // LIFO: take the most-recently-returned connection -- it's the least
    // likely to have been closed by the backend due to idle timeout.
    auto conn = std::move(lst.back());
    lst.pop_back();

    return std::move(*conn.stream);
}

void ConnectionPool::release(int instance_id, beast::tcp_stream stream) {
    if (instance_id < 0 || instance_id >= static_cast<int>(pools_.size())) return;

    auto& slot = pools_[instance_id];
    if (!slot) return;

    std::lock_guard<std::mutex> lock(slot->mutex);
    auto& lst = slot->connections;

    if (static_cast<int>(lst.size()) >= config_.max_idle_per_backend) {
        // stream destructor closes the socket
        return;
    }

    lst.emplace_back(std::move(stream));
}

void ConnectionPool::drain(int instance_id) {
    if (instance_id < 0 || instance_id >= static_cast<int>(pools_.size())) return;

    auto& slot = pools_[instance_id];
    if (!slot) return;

    std::lock_guard<std::mutex> lock(slot->mutex);
    auto& lst = slot->connections;
    std::size_t count = lst.size();
    if (count > 0) {
        lst.clear(); // destructors close the sockets
        spdlog::info("pool: drained {} connections for unhealthy backend instance {}",
                     count, instance_id);
    }
}

void ConnectionPool::start_eviction_timer() {
    schedule_eviction();
}

void ConnectionPool::schedule_eviction() {
    // Run eviction at half the idle timeout interval for timely cleanup.
    auto interval = std::chrono::milliseconds(config_.idle_timeout_ms / 2);
    eviction_timer_.expires_after(interval);

    auto self = shared_from_this();
    eviction_timer_.async_wait([self](beast::error_code ec) {
        if (ec) return; // timer cancelled (shutdown)
        self->evict_expired();
        self->schedule_eviction();
    });
}

void ConnectionPool::evict_expired() {
    auto now = std::chrono::steady_clock::now();
    auto max_idle = std::chrono::milliseconds(config_.idle_timeout_ms);
    std::size_t total_evicted = 0;

    for (auto& slot : pools_) {
        if (!slot) continue;
        std::lock_guard<std::mutex> lock(slot->mutex);
        auto& lst = slot->connections;
        auto before = lst.size();
        // Erase connections that have been idle too long.
        lst.erase(std::remove_if(lst.begin(), lst.end(),
            [&](const PooledConnection& conn) {
                return (now - conn.idle_since) > max_idle;
            }), lst.end());
        total_evicted += (before - lst.size());
    }

    if (total_evicted > 0) {
        spdlog::info("pool: evicted {} expired idle connections", total_evicted);
    }
}
