#include "router.hpp"

void Router::add_route(Route route) {
    routes_.push_back(std::move(route));
}

std::optional<Route> Router::match(http::verb method, std::string_view path) const {
    // Pass 1: exact matches always win outright, since they're maximally specific.
    for (const auto& route : routes_) {
        if (route.method == method
            && route.match_type == MatchType::Exact
            && route.path_pattern == path) {
            return route;
        }
    }

    // Pass 2: among all matching PREFIX routes, the longest one wins.
    const Route* best_match = nullptr;
    std::size_t best_length = 0;

    for (const auto& route : routes_) {
        if (route.method != method || route.match_type != MatchType::Prefix) {
            continue;
        }

        const auto& prefix = route.path_pattern;

        // "starts with" check: path must be at least as long as the prefix,
        // and the first prefix.size() characters must match exactly.
        if (path.size() >= prefix.size() && path.substr(0, prefix.size()) == prefix) {
            if (prefix.size() > best_length) {
                best_match = &route;
                best_length = prefix.size();
            }
        }
    }

    if (best_match != nullptr) {
        return *best_match;
    }

    return std::nullopt;
}