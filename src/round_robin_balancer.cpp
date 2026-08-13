#include "round_robin_balancer.hpp"

BackendInstance* RoundRobinBalancer::select(std::vector<BackendInstance>& instances) {
    if (instances.empty()) {
        return nullptr;
    }

    // fetch_add atomically reads the current value AND increments it in one
    // indivisible step -- two threads calling this simultaneously are
    // guaranteed to get two DIFFERENT values back, never the same one twice.
    // That's what makes this safe with zero mutex, from many threads at once.
    for (std::size_t attempts = 0; attempts < instances.size(); ++attempts) {
        std::size_t index = next_.fetch_add(1, std::memory_order_relaxed) % instances.size();
        if (instances[index].is_healthy.load(std::memory_order_relaxed)) {
            return &instances[index];
        }
    }
    return nullptr; //every instance unhealthy
}