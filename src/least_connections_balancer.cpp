#include "least_connections_balancer.hpp"

BackendInstance* LeastConnectionsBalancer::select(std::vector<BackendInstance>& instances) {
    if(instances.empty()) return nullptr;

    BackendInstance* best = nullptr;
    int best_count = 0;

    for (auto& instance : instances) {
        if (!instance.is_healthy.load(std::memory_order_relaxed)) {
            continue; // skip unhealthy instances entirely
        }

        int count = instance.active_connections.load(std::memory_order_relaxed);
        if (best == nullptr || count < best_count) {
            best = &instance;
            best_count = count;
        }
    }

    return best; // nullptr if none were healthy
}