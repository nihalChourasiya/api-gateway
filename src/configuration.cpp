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
            service.instances.emplace_back(host, port);
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

    spdlog::info("configuration loaded: {} services, {} routes", service_count, route_count);
    return Configuration{router, registry};
}