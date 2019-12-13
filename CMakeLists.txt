cmake_minimum_required(VERSION 3.13)

project(Single-Task-Execution)

list(APPEND SRCS src/main.c)

add_executable(single-ex ${SRCS})