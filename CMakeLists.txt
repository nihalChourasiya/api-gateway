cmake_minimum_required(VERSION 3.21)

project(api_gateway VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS asio beast)
find_package(yaml-cpp CONFIG REQUIRED)

add_executable(api_gateway
    src/main.cpp
    src/http_connection.cpp
    src/router.cpp
    src/http_client.cpp
    src/configuration.cpp
    src/service_registry.cpp
    src/round_robin_balancer.cpp
    src/least_connections_balancer.cpp
)

target_link_libraries(api_gateway
    PRIVATE
        Threads::Threads
        spdlog::spdlog
        Boost::asio
        Boost::beast
        yaml-cpp::yaml-cpp
)