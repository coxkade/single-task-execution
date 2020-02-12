cmake_minimum_required(VERSION 3.13)

enable_testing ()

project(Single-Task-Execution)

add_subdirectory(simply-thread)

list(APPEND SRCS src/main.c)

add_custom_target(single-task-lib-coverage DEPENDS simply-thread-coverage)