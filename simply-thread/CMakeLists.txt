cmake_minimum_required(VERSION 3.13)

project(simply-thread)


add_library(simply-thread STATIC 
    src/simply-thread.c 
    src/simply-thread-log.c 
    src/simply-thread-linked-list.c
    src/simply-thread-scheduler.c)
    
SET_TARGET_PROPERTIES(simply-thread PROPERTIES COMPILE_FLAGS "-O0 -Wall -Werror")
target_include_directories(simply-thread PUBLIC inc)
target_include_directories(simply-thread PRIVATE src/priv-inc)