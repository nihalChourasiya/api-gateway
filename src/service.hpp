#pragma once

#include "backend_instance.hpp"
#include "load_balancer.hpp"
#include <memory>
#include <string>
#include <vector>

struct Service {
    std::string name;
    std::vector<BackendInstance> instances;
    std::unique_ptr<LoadBalancer> balancer;
};