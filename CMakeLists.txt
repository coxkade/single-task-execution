cmake_minimum_required(VERSION 3.13)

project(Single-Task-Execution)

add_subdirectory(simply-thread)

list(APPEND SRCS src/main.c)

add_executable(single-ex ${SRCS})
target_link_libraries(single-ex simply-thread)