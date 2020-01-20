/**
 * @file main_test.c
 * @author Kade Cox
 * @date Created: Jan 17, 2020
 * @details
 * The Main File Holding the tests for simply thread
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <simply-thread.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static simply_thread_task_t task_one = NULL;  //The First tasks task handle
static simply_thread_task_t task_two = NULL; //The Second tasks task handle
static bool thread_one_ran = false; //Tells if thread one ever ran
static bool thread_two_ran = false; //Tells if thread two ever ran
static simply_thread_timer_t timer_1; //The handle for timer one
static simply_thread_timer_t timer_2; //The handle for timer two
static bool timer_1_ran = false; //Tells if timer 1 executed
static unsigned int timer_2_count = 0; //The count of timer 2

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


/**
 * @brief the task function for the second task
 * @param data UNUSED
 * @param data_size UNUSED
 */
static void thread_two_worker(void *data, uint16_t data_size)
{
    simply_thread_sleep_ms(100);
    while(1)
    {
        assert_true(true == simply_thread_task_suspend(NULL));
        thread_two_ran = true;
    }
}

/**
 * @brief The function for the first task
 * @param data UNUSED
 * @param data_size UNUSED
 */
static void thread_one_worker(void *data, uint16_t data_size)
{
    simply_thread_sleep_ms(100);
    while(1)
    {
        assert_true(true == simply_thread_task_resume(task_two));
        simply_thread_sleep_ms(10);
        thread_one_ran = true;
    }
}



static void task_test_success(void **state)
{
    simply_thread_reset();
    task_one = simply_thread_new_thread("TASK1", thread_one_worker, 1, NULL, 0);
    task_two = simply_thread_new_thread("TASK2", thread_two_worker, 3, NULL, 0);

    assert_true(NULL != task_one);
    assert_true(NULL != task_two);
    assert_false(simply_thread_task_suspend(NULL));
    assert_false(simply_thread_task_resume(NULL));
    simply_thread_sleep_ms(500);
    simply_thread_cleanup();
    assert_true(thread_one_ran);
    assert_true(thread_two_ran);
}


static bool task_non_null_data_test_continue = false;
/**
 * Third worker thread to test tasks with data
 * @param data
 * @param data_size
 */
static void thread_three_worker(void *data, uint16_t data_size)
{
    simply_thread_sleep_ms(100);
    int test_val;
    assert_int_equal(data_size, sizeof(test_val));
    assert_true(NULL != data);
    memcpy(&test_val, data, sizeof(test_val));
    assert_int_equal(5, test_val);
    task_non_null_data_test_continue = true;
    while(1)
    {
        simply_thread_sleep_ms(100);
    }
}

/**
 * @brief Test for non NULL data
 * @param state
 */
static void task_non_null_data_test(void **state)
{
    simply_thread_task_t test_task;
    int test_val = 5;
    task_non_null_data_test_continue = false;
    simply_thread_reset();
    test_task = simply_thread_new_thread("DataTask", thread_three_worker, 1, &test_val, sizeof(test_val));
    assert_true(NULL != test_task);

    simply_thread_sleep_ms(300);
    assert_true(task_non_null_data_test_continue);
    simply_thread_cleanup();
}

static void first_timer_worker(simply_thread_timer_t timer)
{
    assert_true(timer_1 == timer);
    timer_1_ran = true;
}

static void second_timer_worker(simply_thread_timer_t timer)
{
    timer_2_count++;
}

static void timer_test(void **state)
{
    simply_thread_reset();
    task_one = simply_thread_new_thread("TASK1", thread_one_worker, 1, NULL, 0);
    task_two = simply_thread_new_thread("TASK2", thread_two_worker, 3, NULL, 0);

    assert_true(NULL != task_one);
    assert_true(NULL != task_two);
    assert_false(simply_thread_task_suspend(NULL));
    assert_false(simply_thread_task_resume(NULL));

    assert_true(NULL == simply_thread_create_timer(NULL, "Hello", 5, SIMPLY_THREAD_TIMER_ONE_SHOT, true));
    timer_1 = simply_thread_create_timer(first_timer_worker, "Timer One", 100, SIMPLY_THREAD_TIMER_ONE_SHOT, true);
    assert_true(NULL != timer_1);
    timer_2 = simply_thread_create_timer(second_timer_worker, "Timer two", 100, SIMPLY_THREAD_TIMER_REPEAT, true);
    assert_true(NULL != timer_2);
    assert_true(simply_thread_timer_stop(timer_1));
    assert_false(simply_thread_timer_start(NULL));
    assert_false(simply_thread_timer_stop(NULL));
    assert_true(simply_thread_timer_start(timer_1));
    simply_thread_sleep_ms(510);
    simply_thread_cleanup();
    assert_true(thread_one_ran);
    assert_true(thread_two_ran);
    assert_true(timer_1_ran);
    assert_int_equal(5, timer_2_count);
}

static void main_timer_tests(void **state)
{
    timer_2_count = 0;
    timer_test(state);
}

static void second_timer_tests(void **state)
{
    timer_2_count = 0;
    timer_test(state);
}

/**
 * @brief the main function
 * @return
 */
int main(void)
{
    const struct CMUnitTest tests[] =
    {
        cmocka_unit_test(task_test_success),
        cmocka_unit_test(task_non_null_data_test),
        cmocka_unit_test(main_timer_tests),
        cmocka_unit_test(second_timer_tests)
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
