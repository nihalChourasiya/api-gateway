#pragma once

#include "router.hpp"
#include "service_registry.hpp"
#include <memory>
#include <string>

// Result of loading and validating gateway.yaml: a ready-to-use Router and
// ServiceRegistry, both immutable from this point on (per the concurrency
// plan -- read-only after startup means no locking needed later).
struct Configuration {
    std::shared_ptr<Router> router;
    std::shared_ptr<ServiceRegistry> registry;

    // Throws std::runtime_error with a specific message on any validation
    // failure -- callers should treat that as "do not start the server."
    static Configuration load(const std::string& path);
};