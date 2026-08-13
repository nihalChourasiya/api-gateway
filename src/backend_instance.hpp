#pragma once

#include <atomic>
#include <string>

struct BackendInstance {
    std::string host;
    unsigned short port;
    std::atomic<int> active_connections{0};

    BackendInstance(std::string h, unsigned short p)
        : host(std::move(h)), port(p) {}

    // std::atomic<int> has NO copy constructor and NO move constructor --
    // copying/moving an atomic isn't a well-defined operation in general.
    // But during config loading, this all happens on a single thread before
    // any request traffic exists, so it's safe to define our own move that
    // just carries the counter's starting value (0) along by hand.
    BackendInstance(BackendInstance&& other) noexcept
        : host(std::move(other.host)),
          port(other.port),
          active_connections(other.active_connections.load()) {}

    BackendInstance(const BackendInstance&) = delete;
    BackendInstance& operator=(const BackendInstance&) = delete;
    BackendInstance& operator=(BackendInstance&&) = delete;
};