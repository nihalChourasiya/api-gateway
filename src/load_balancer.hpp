#pragma once

#include "backend_instance.hpp"
#include <vector>

// Abstract strategy interface. A Service owns exactly one of these, chosen
// by config, and calls select() on every request that targets it.
class LoadBalancer {
public:
    virtual ~LoadBalancer() = default;

    // Returns the chosen instance, or nullptr if the list is empty.
    // (Health-aware filtering -- skipping unhealthy instances -- gets
    // added here in Phase 7; for now every instance is considered eligible.)
    virtual BackendInstance* select(std::vector<BackendInstance>& instances) = 0;
};