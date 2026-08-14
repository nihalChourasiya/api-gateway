#include "configuration.hpp"
#include "round_robin_balancer.hpp"
#include "least_connections_balancer.hpp"
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

Configuration Configuration::load(const std::string& path) {
    YAML::Node root = YAML::LoadFile(path); // throws if the file is missing/unparseable

    auto registry = std::make_shared<ServiceRegistry>();
    auto router = std::make_shared<Router>();

    // ---- services ----
    if (!root["services"]) {
        throw std::runtime_error("config error: no 'services' section found in " + path);
    }

    int service_count = 0;
    int global_instance_id = 0;
    for (const auto& service_entry : root["services"]) {
        Service service;
        service.name = service_entry.first.as<std::string>();

        // Phase 6: choose a load balancer strategy per service
        std::string strategy = service_entry.second["load_balancing"]
            ? service_entry.second["load_balancing"].as<std::string>()
            : "round_robin"; // sensible default if the field is omitted

        if (strategy == "round_robin") {
            service.balancer = std::make_unique<RoundRobinBalancer>();
        } else if (strategy == "least_connections") {
            service.balancer = std::make_unique<LeastConnectionsBalancer>();
        } else {
            throw std::runtime_error(
                "config error: unknown load_balancing strategy '" + strategy
                    + "' for service '" + service.name + "'");
        }

        auto instances_node = service_entry.second["instances"];
        if (!instances_node || instances_node.size() == 0) {
            throw std::runtime_error(
                "config error: service '" + service.name + "' has no instances defined");
        }

        for (const auto& inst : instances_node) {
            std::string host = inst["host"].as<std::string>();
            unsigned short port = inst["port"].as<unsigned short>();
            service.instances.emplace_back(host, port, global_instance_id++);
        }

        registry->add_service(std::move(service));
        ++service_count;
    }

    // ---- routes ----
    if (!root["routes"]) {
        throw std::runtime_error("config error: no 'routes' section found in " + path);
    }

    int route_count = 0;
    for (const auto& route_node : root["routes"]) {
        Route route;

        std::string method_str = route_node["method"] ? route_node["method"].as<std::string>() : "GET";
        route.method = http::string_to_verb(method_str);

        std::string service_name = route_node["service"].as<std::string>();

        // Fail fast: a route pointing at an unknown service is a config bug,
        // not something to discover later as a mystery 404 in production.
        if (registry->find(service_name) == nullptr) {
            throw std::runtime_error(
                "config error: route references unknown service '" + service_name + "'");
        }
        route.service_name = service_name;

        if (route_node["path_prefix"]) {
            route.path_pattern = route_node["path_prefix"].as<std::string>();
            route.match_type = MatchType::Prefix;
        } else if (route_node["path"]) {
            route.path_pattern = route_node["path"].as<std::string>();
            route.match_type = MatchType::Exact;
        } else {
            throw std::runtime_error("config error: route must specify 'path' or 'path_prefix'");
        }

        router->add_route(std::move(route));
        ++route_count;
    }

    HealthCheckConfig health_check; // defaults already set in the struct

    if (root["health_check"]) {
        auto hc = root["health_check"];
        if (hc["interval_ms"])      health_check.interval = std::chrono::milliseconds(hc["interval_ms"].as<int>());
        if (hc["timeout_ms"])       health_check.timeout = std::chrono::milliseconds(hc["timeout_ms"].as<int>());
        if (hc["unhealthy_after"])  health_check.unhealthy_after = hc["unhealthy_after"].as<int>();
        if (hc["healthy_after"])    health_check.healthy_after = hc["healthy_after"].as<int>();
    }

    PoolConfig pool_config; // defaults already set in the struct

    if (root["connection_pool"]) {
        auto cp = root["connection_pool"];
        if (cp["max_idle_per_backend"])  pool_config.max_idle_per_backend = cp["max_idle_per_backend"].as<int>();
        if (cp["idle_timeout_ms"])       pool_config.idle_timeout_ms = cp["idle_timeout_ms"].as<int>();
    }

    spdlog::info("configuration loaded: {} services, {} routes, pool(max_idle={}, idle_timeout={}ms)",
                 service_count, route_count, pool_config.max_idle_per_backend, pool_config.idle_timeout_ms);
    
    // Set total instances count in pool config to pre-allocate vector
    int total_instances = 0;
    for (const auto& pair : registry->all_services()) {
        total_instances += pair.second.instances.size();
    }
    pool_config.total_instances = total_instances;
    
    return Configuration{router, registry, health_check, pool_config};
}