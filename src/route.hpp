#pragma once

#include <boost/beast/http.hpp>
#include <string>

namespace http = boost::beast::http;

enum class MatchType {
    Exact,
    Prefix
};

struct Route {
    http::verb method;          // e.g. http::verb::get
    std::string path_pattern;   // e.g. "/users"
    MatchType match_type;
    std::string service_name;   // e.g. "user-service" -- just a label for now
};