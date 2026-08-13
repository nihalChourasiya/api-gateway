#pragma once

#include "service.hpp"
#include <string>
#include <unordered_map>

class ServiceRegistry {
public:
    void add_service(Service service);

    // Returns nullptr if no service with this name is registered.
    Service* find(const std::string& name);

private:
    std::unordered_map<std::string, Service> services_;
};