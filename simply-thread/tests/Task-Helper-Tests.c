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
#include <Thread-Helper.h>
#include <Sem-Helper.h>
#include <TCB.h>

//#define DEBUG_TESTS

#ifdef DEBUG_TESTS
#define PRINT_MSG(...) SIMPLY_THREAD_PRINT(__VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_TESTS

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

/**
 * @brief Function that sleeps for the specified number of nanoseconds
 * @param ns number of nanoseconds to sleep
 */
void test_thread_sleep_ns(unsigned long ns)
{
    struct timespec time_data =
    {
        .tv_sec = 0,
        .tv_nsec = ns
    };

    while(0 != nanosleep(&time_data, &time_data))
    {
    }
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
    SS_ASSERT(NULL != typed);
    while(1)
    {
        if(typed->count < typed->target_count)
        {
            typed->count++;
            if(typed->count == typed->target_count)
            {
//                PRINT_MSG("Target %u Hit\r\n", typed->target_count);
            }
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
    unsigned int last_count;
    helper_thread_t *test_thread;
    PRINT_MSG("Resetting the thread helper\r\n");
    reset_thread_helper();
    Sem_Helper_clear();
    PRINT_MSG("Creating Thread\r\n");
    test_thread = thread_helper_thread_create(thread_worker_test_task, &count_data);
    SS_ASSERT(NULL != test_thread);
    PRINT_MSG("Waiting on target count\r\n");
    while(count_data.target_count != count_data.count) {}
    SS_ASSERT(count_data.target_count <= count_data.count);
    PRINT_MSG("Checking that the thread is running\r\n");
    SS_ASSERT(true == thread_helper_thread_running(test_thread));
    PRINT_MSG("Checking the thread ID fails\r\n");
    SS_ASSERT(thread_helper_get_id(test_thread) != pthread_self());
    PRINT_MSG("Pausing the Thread\r\n");
    thread_helper_pause_thread(test_thread);
    last_count = count_data.count;
    PRINT_MSG("Checking that thread is not running\r\n");
    SS_ASSERT(false == thread_helper_thread_running(test_thread));
    count_data.target_count = count_data.count + 1000;
    PRINT_MSG("Checking that the thread is not running\r\n");
    SS_ASSERT(false == thread_helper_thread_running(test_thread));
    test_thread_sleep_ns(50000);
    SS_ASSERT(last_count == count_data.count);
    PRINT_MSG("Starting the thread\r\n");
    thread_helper_run_thread(test_thread);
    PRINT_MSG("Waiting on the Count\r\n");
    while(count_data.target_count != count_data.count) {}
    SS_ASSERT(count_data.target_count <= count_data.count);
    PRINT_MSG("Pausing the thread\r\n");
    thread_helper_pause_thread(test_thread);
    PRINT_MSG("Checking that the thread is not running\r\n");
    SS_ASSERT(false == thread_helper_thread_running(test_thread));
    PRINT_MSG("Destroying the thread\r\n");
    thread_helper_thread_destroy(test_thread);
    reset_thread_helper();
    Sem_Helper_clear();
    thread_helper_cleanup();
    Sem_Helper_clean_up();
}


/*********************************************************************
 ************************* TCB Test Items ****************************
 ********************************************************************/

static tcb_task_t *tcb_task_one;
static tcb_task_t *tcb_task_two;
static unsigned int tcb_one_count;
static unsigned int tcb_two_count;
static bool tcb_test_run_ran;

static void tcb_worker_two(void *data, uint16_t data_size)
{
    pthread_t me;
    while(NULL == tcb_task_two) {}
    me = pthread_self();
    SS_ASSERT(NULL == data);
    SS_ASSERT(0 == data_size);
    while(1)
    {
        PRINT_MSG("%s Setting task one to ready\r\n", __FUNCTION__);
        tcb_set_task_state(SIMPLY_THREAD_TASK_READY, tcb_task_one);
        tcb_two_count++;
    }
}

static void tcb_worker_one(void *data, uint16_t data_size)
{
    pthread_t me;
    unsigned int *typed;
    tcb_task_t *task_ptr;
    typed = data;

    while(NULL == tcb_task_one) {}

    me = pthread_self();
    SS_ASSERT(sizeof(unsigned int) == data_size);
    SS_ASSERT(NULL != typed);
    SS_ASSERT(200 == typed[0]);

    task_ptr = tcb_task_self();

    SS_ASSERT(NULL != task_ptr);

    PRINT_MSG("%s Creating Task Two\r\n", __FUNCTION__);
    SS_ASSERT(NULL == tcb_task_two);
    tcb_task_two = tcb_create_task("TASK TWO", tcb_worker_two, 1, NULL, 0);
    SS_ASSERT(NULL != tcb_task_two);
    while(1)
    {
        PRINT_MSG("%s suspending task 1\r\n", __FUNCTION__);
        tcb_set_task_state(SIMPLY_THREAD_TASK_SUSPENDED, tcb_task_one);
        tcb_one_count++;
    }
}

static void tcb_run_test(void *data)
{
    SS_ASSERT(data == (void *)5);
    tcb_test_run_ran = true;
}

static void basic_TCB_test(void **state)
{

    unsigned int value = 200;

    tcb_task_one = NULL;
    tcb_task_two = NULL;
    tcb_one_count = 0;
    tcb_two_count = 0;
    tcb_test_run_ran = false;

    tcb_reset();
    printf("Running tcb_run_test in the TCB Context\r\n");
    run_in_tcb_context(tcb_run_test, (void *)5);
    //Create tcb worker one
    PRINT_MSG("Creating Task 1\r\n");
    tcb_task_one = tcb_create_task("TASK ONE", tcb_worker_one, 2, &value, sizeof(value));
    PRINT_MSG("Task 1 Created\r\n");
    SS_ASSERT(NULL != tcb_task_one);
    while(NULL == tcb_task_two) {}
    PRINT_MSG("Checking tcb_task_self\r\n");
    SS_ASSERT(NULL == tcb_task_self());
    PRINT_MSG("Finishing up the Tests\r\n");
    while(500 > tcb_two_count) {}
    SS_ASSERT(500 <= tcb_two_count);
    SS_ASSERT(500 <= tcb_one_count);
    tcb_reset();
    SS_ASSERT(true == tcb_test_run_ran);
}

static void TCB_Test_One(void **state)
{
    basic_TCB_test(state);
}

static void TCB_Test_Two(void **state)
{
    basic_TCB_test(state);
}

/**
 * @brief run the task helper tests
 */
int run_task_helper_tests(void)
{
    int rv;
    const struct CMUnitTest message_helper_tests[] =
    {
		cmocka_unit_test(basic_thread_helper_test),
        cmocka_unit_test(TCB_Test_One),
        cmocka_unit_test(TCB_Test_Two)
    };
    rv = cmocka_run_group_tests(message_helper_tests, NULL, NULL);
    SS_ASSERT(0 <= rv);
    return 0;
}
