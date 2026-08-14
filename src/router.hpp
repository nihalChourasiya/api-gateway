#pragma once

#include "route.hpp"
#include <optional>
#include <string_view>
#include <vector>

class Router {
public:
    void add_route(Route route);

    // Returns the matching Route, or nullptr if nothing matches.
    const Route* match(http::verb method, std::string_view path) const;

private:
    std::vector<Route> routes_;
};