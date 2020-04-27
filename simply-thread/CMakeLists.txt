cmake_minimum_required(VERSION 3.13)

#Macro that configures the cmake flags for the unit tests
macro(set_test_cmake_flags TARG)
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
	message(STATUS "Configuring for Mac")
	target_compile_options(${TARG} PUBLIC -g -O0 -Wall -fprofile-arcs -ftest-coverage -std=c11)
elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
	message(STATUS "Configuring for linux")
	target_compile_options(${TARG} PUBLIC -g -O0 -Wall -fprofile-arcs -ftest-coverage -std=c11 -D_POSIX_C_SOURCE=200809L)
else()
    message( FATAL_ERROR "Cannot Configure for ${CMAKE_HOST_SYSTEM_NAME}")
endif()

endmacro(set_test_cmake_flags)

#Macro that configures the cmake flags for the unit tests
macro(set_lib_cmake_flags TARG)
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    message(STATUS "Configuring for Mac")
	target_compile_options(${TARG} PUBLIC -g -O0 -Wall -std=c11)
elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
	message(STATUS "Configuring for linux")
	target_compile_options(${TARG} PUBLIC -g -O0 -Wall -std=c11 -D_POSIX_C_SOURCE=200809L)
else()
    message( FATAL_ERROR "Cannot Configure for ${CMAKE_HOST_SYSTEM_NAME}")
endif()

endmacro(set_lib_cmake_flags)


#Macro that does some test prep
macro( simply_thred_test_prep )
    find_library(MOCKA_LIB cmocka)
    find_path(MOCKA_PATH cmocka.h)
    if(NOT MOCKA_LIB)
    message(FATAL_ERROR "cmocka not found.  Make sure it is installed and supported by your toolchain.")
    endif()
    if(NOT MOCKA_PATH)
    message(FATAL_ERROR "cmocka path not found.  Make sure it is installed and supported by your toolchain.")
    endif()
    # Threads Are Required for Posix Hal Components
    set(THREADS_PREFER_PTHREAD_FLAG ON) 
    find_package(Threads REQUIRED)
    find_program(GCOV gcov)
    if(NOT GCOV)
        message(FATAL_ERROR "gcov not found.  Make sure it is installed and supported by your toolchain.")
    endif()
    
endmacro(simply_thred_test_prep)

#macro that sets up a gcov target
macro(create_gcov_rule src)
	message(STATUS "Setting up Coverage for ${src}")
	get_filename_component(just_name ${src} NAME)
	get_filename_component(barename ${src} NAME)
	set(cov_name ${just_name}.gcov)
	message(STATUS "Setting up Coverage for ${cov_name}")
	
	LIST(APPEND ALL_GCOV ${cov_name})
	
	add_custom_command(
        OUTPUT ${cov_name}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gcov.py --gcov ${GCOV} --source ${src} --verbose
        DEPENDS run-simply-thread-test
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endmacro(create_gcov_rule src)

find_package (Python3 3 REQUIRED)

set( ENV{CTEST_OUTPUT_ON_FAILURE} TRUE)

LIST(APPEND simply-thread-sources 
	src/simply-thread.c 
    src/simply-thread-log.c 
    src/simply-thread-timers.c
	src/simply-thread-mutex.c
	#src/simply-thread-queue.c
	src/simply_thread_system_clock.c
	src/Task-Helper/Thread-Helper.c
	src/Task-Helper/TCB.c
	src/Task-Helper/Sem-Helper.c
	src/Task-Helper/que-creator.c
	)

#project for the simply thread tests
project(simply-thread-tests)
	set(ENV{CMOCKA_TEST_ABORT} 1)
	add_executable(simply-thread-test-ex tests/main_test.c tests/Task-Helper-Tests.c ${simply-thread-sources})
	simply_thred_test_prep()
	set_test_cmake_flags(simply-thread-test-ex)
	target_include_directories(simply-thread-test-ex PRIVATE ${MOCKA_PATH} inc src/priv-inc src/Task-Helper tests)
	target_link_libraries(simply-thread-test-ex ${MOCKA_LIB} ${CMAKE_THREAD_LIBS_INIT} -fprofile-arcs -ftest-coverage)
	add_test(NAME simply-thread-test 
		COMMAND simply-thread-test-ex
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})		
	
	
	#setup the coverage
	add_custom_target(run-simply-thread-test
		COMMAND simply-thread-test-ex
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		DEPENDS simply-thread-test-ex)
	
	foreach(sourcefile ${simply-thread-sources})
		create_gcov_rule(${sourcefile})
	endforeach(sourcefile ${simply-thread-sources})
	
	add_custom_target(simply-thread-coverage
		DEPENDS ${ALL_GCOV}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

project(simply-thread)

	#Source for the simply thread Library
	add_library(simply-thread STATIC 
   	${simply-thread-sources})
   	set_lib_cmake_flags(simply-thread)
	#SET_TARGET_PROPERTIES(simply-thread PROPERTIES COMPILE_FLAGS ${MAIN_CFLAGS})
	target_include_directories(simply-thread PUBLIC inc)
	target_include_directories(simply-thread PRIVATE src/priv-inc src/Task-Helper)


project(sem-clean-tool)
	add_custom_target(
		semaphore-clean
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/ipcs-cleanup.py
	)

		