#pragma once

#include "backend_instance.hpp"

// Increments an instance's active_connections when constructed, decrements
// it when destroyed -- automatically, on every code path, including ones
// that "return early" or throw. This is RAII applied to a plain counter,
// not just to memory/file handles.
class ConnectionGuard {
public:
    explicit ConnectionGuard(BackendInstance& instance) : instance_(instance) {
        instance_.active_connections.fetch_add(1, std::memory_order_relaxed);
    }

    ~ConnectionGuard() {
        instance_.active_connections.fetch_sub(1, std::memory_order_relaxed);
    }

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

private:
    BackendInstance& instance_;
};