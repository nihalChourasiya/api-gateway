#pragma once

#include "load_balancer.hpp"
#include <atomic>

class RoundRobinBalancer : public LoadBalancer {
public:
    BackendInstance* select(std::vector<BackendInstance>& instances) override;

private:
    std::atomic<std::size_t> next_{0};
};