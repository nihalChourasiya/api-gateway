#include "service_registry.hpp"

void ServiceRegistry::add_service(Service service) {
    services_.emplace(service.name, std::move(service));
}

Service* ServiceRegistry::find(const std::string& name) {
    auto it = services_.find(name);
    if (it == services_.end()) {
        return nullptr;
    }
    return &it->second;
}