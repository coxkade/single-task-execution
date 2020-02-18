cmake_minimum_required(VERSION 3.13)

if(NS_PER_MS)
	message(STATUS "NS_PER_MS override set to ${NS_PER_MS}")
    set(CMAKE_C_FLAGS -DST_NS_PER_MS=${NS_PER_MS})
else()
    set(CMAKE_C_FLAGS -DST_NS_PER_MS=1000000)
endif(NS_PER_MS)

enable_testing ()

project(Single-Task-Execution)

add_subdirectory(simply-thread)

add_custom_target(single-task-lib-coverage DEPENDS simply-thread-coverage)