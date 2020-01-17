cmake_minimum_required(VERSION 3.13)

enable_testing ()

project(Single-Task-Execution)

add_subdirectory(simply-thread)

list(APPEND SRCS src/main.c)

add_executable(single-ex ${SRCS})

# target_compile_definitions(simply-thread PRIVATE -DDEBUG_SIMPLY_THREAD)
target_link_libraries(single-ex simply-thread)

add_custom_target(coverage DEPENDS simply-thread-coverage)