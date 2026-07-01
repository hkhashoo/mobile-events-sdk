cmake_minimum_required(VERSION 3.18)
project(EventSDK VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(eventsdk STATIC
    sdk-core/src/EventQueue.cpp
    sdk-core/src/EventStore.cpp
    sdk-core/src/FlushManager.cpp
)
target_include_directories(eventsdk PUBLIC sdk-core/src)

add_executable(eventsdk_tests tests/test_main.cpp)
target_link_libraries(eventsdk_tests PRIVATE eventsdk)

add_executable(eventsdk_bench benchmarks/bench_main.cpp)
target_link_libraries(eventsdk_bench PRIVATE eventsdk)

enable_testing()
add_test(NAME unit_tests COMMAND eventsdk_tests)
