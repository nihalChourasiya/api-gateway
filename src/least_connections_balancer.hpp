#pragma once

#include "load_balancer.hpp"

class LeastConnectionsBalancer : public LoadBalancer {
public:
    BackendInstance* select(std::vector<BackendInstance>& instances) override;
};