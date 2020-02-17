cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_FLAGS -DST_NS_PER_MS=1000000)

enable_testing ()

project(Single-Task-Execution)

add_subdirectory(simply-thread)

add_custom_target(single-task-lib-coverage DEPENDS simply-thread-coverage)