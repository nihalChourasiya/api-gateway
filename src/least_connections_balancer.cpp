#include "least_connections_balancer.hpp"

BackendInstance* LeastConnectionsBalancer::select(std::vector<BackendInstance>& instances) {
    if (instances.empty()) {
        return nullptr;
    }

    BackendInstance* best = &instances[0];
    int best_count = best->active_connections.load(std::memory_order_relaxed);

    for (auto& instance : instances) {
        int count = instance.active_connections.load(std::memory_order_relaxed);
        if (count < best_count) {
            best = &instance;
            best_count = count;
        }
    }

    return best;
}