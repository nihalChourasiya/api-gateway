#pragma once

#include "service.hpp"
#include <string>
#include <unordered_map>

class ServiceRegistry {
public:
    void add_service(Service service);

    // Returns nullptr if no service with this name is registered.
    Service* find(const std::string& name);

    // Used by HealthChecker to probe every instance of every service.
    std::unordered_map<std::string, Service>& all_services() { return services_; }

private:
    std::unordered_map<std::string, Service> services_;
};