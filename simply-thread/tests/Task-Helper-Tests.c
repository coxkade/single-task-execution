/*
 * Task-Helper-Tests.c
 *
 *  Created on: Mar 7, 2020
 *      Author: kade
 */

#include <Task-Helper-Tests.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <Message-Helper.h>
#include <Thread-Helper.h>

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

/*********************************************************************
 ************** Message Helper Test Items ****************************
 ********************************************************************/

#define BASIC_MSG_HELPER_TEST_MSG_SIZE 30
static bool basic_message_helper_wait;

/**
 * The on message callback for the basic message helper test
 * @param message
 * @param message_size
 */
static void message_helper_test_worker(void *message, uint32_t message_size)
{
    char expected_msg[BASIC_MSG_HELPER_TEST_MSG_SIZE];
    memset(expected_msg, 0xAB, ARRAY_MAX_COUNT(expected_msg));
    assert_non_null(message);
    assert_int_equal(ARRAY_MAX_COUNT(expected_msg), message_size);
    assert_memory_equal(message, expected_msg, message_size);
    basic_message_helper_wait = false;
}

/**
 * @brief the basic message helper test
 * @param state
 */
static void basic_message_helper_test(void **state)
{
    char test_msg[BASIC_MSG_HELPER_TEST_MSG_SIZE];
    basic_message_helper_wait = true;
    memset(test_msg, 0xAB, ARRAY_MAX_COUNT(test_msg));
    Message_Helper_Instance_t *helper;
    helper = New_Message_Helper(message_helper_test_worker);
    assert_true(NULL != helper);
    Message_Helper_Send(helper, test_msg, ARRAY_MAX_COUNT(test_msg));
    while(true == basic_message_helper_wait) {}
    assert_false(basic_message_helper_wait);
    Remove_Message_Helper(helper);
}

/*********************************************************************
 *************** Thread Helper Test Items ****************************
 ********************************************************************/

struct thread_test_data_s
{
    unsigned int count;
    unsigned int target_count;
}; //!< Structure to help with the thread helper test

/**
 * @brief Thread helper test worker function
 * @param data
 */
static void *thread_worker_test_task(void *data)
{
    struct thread_test_data_s *typed = data;
    assert_non_null(typed);
    while(1)
    {
        if(typed->count < typed->target_count)
        {
            typed->count++;
        }
    }
    return NULL;
}

/**
 * @brief the basic message helper test
 * @param state
 */
static void basic_thread_helper_test(void **state)
{
    struct thread_test_data_s count_data =
    {
        .count = 0,
        .target_count = 500
    };
    helper_thread_t *test_thread;
    test_thread = thread_helper_thread_create(thread_worker_test_task, &count_data);
    assert_non_null(test_thread);
    while(count_data.target_count != count_data.count) {}
    assert_int_equal(count_data.target_count, count_data.count);
    assert_true(thread_helper_thread_running(test_thread));
    thread_helper_pause_thread(test_thread);
    assert_false(thread_helper_thread_running(test_thread));
    count_data.target_count = 1000;
    assert_false(thread_helper_thread_running(test_thread));
    thread_helper_run_thread(test_thread);
    while(count_data.target_count != count_data.count) {}
    assert_int_equal(count_data.target_count, count_data.count);
    thread_helper_pause_thread(test_thread);
    assert_false(thread_helper_thread_running(test_thread));
    thread_helper_thread_destroy(test_thread);
}

/**
 * @brief run the task helper tests
 */
int run_task_helper_tests(void)
{
    int rv;
    const struct CMUnitTest message_helper_tests[] =
    {
        cmocka_unit_test(basic_message_helper_test),
        cmocka_unit_test(basic_thread_helper_test)
    };
    rv = cmocka_run_group_tests(message_helper_tests, NULL, NULL);
    assert(0 <= rv);
    return 0;
}
