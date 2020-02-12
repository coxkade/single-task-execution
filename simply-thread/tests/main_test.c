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

#ifdef DEBUG_TESTS
#define PRINT_MSG(...) printf(__VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_TESTS

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
static simply_thread_mutex_t mutex_handles[10]; // Array of mutex handles I can use in the tests
static simply_thread_queue_t queue_handles[10]; // array of queue handles

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

    simply_thread_sleep_ms(1000);
    assert_true(task_non_null_data_test_continue);
    simply_thread_cleanup();
}

/*********************************************************************
 *********************** Timer Test Items ****************************
 ********************************************************************/

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
    assert_true(simply_thread_timer_stop(timer_1));
    assert_false(simply_thread_timer_start(NULL));
    assert_false(simply_thread_timer_stop(NULL));
    assert_true(simply_thread_timer_start(timer_1));
    timer_2 = simply_thread_create_timer(second_timer_worker, "Timer two", 100, SIMPLY_THREAD_TIMER_REPEAT, true);
    assert_true(NULL != timer_2);
    simply_thread_sleep_ms(540);
    assert_true(simply_thread_timer_stop(timer_2));
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


/*********************************************************************
 *********************** Mutex Test Items ****************************
 ********************************************************************/

static void mutex_worker_1_task(void *data, uint16_t data_size)
{
    simply_thread_mutex_t *m_handle = data;
    assert_true(NULL != m_handle);
    assert_true(sizeof(simply_thread_mutex_t) == data_size);
    simply_thread_sleep_ms(25);
    assert_true(simply_thread_mutex_lock(mutex_handles[0], 0xFFFFFFFF));
    assert_true(simply_thread_mutex_unlock(mutex_handles[0]));
    while(1)
    {
        assert_true(simply_thread_mutex_lock(m_handle[0], 0xFFFFFFFF));
        thread_one_ran = true;
        simply_thread_sleep_ms(25);
        assert_true(simply_thread_mutex_unlock(m_handle[0]));
        simply_thread_sleep_ms(100);
    }
}

static void mutex_worker_2_task(void *data, uint16_t data_size)
{
    simply_thread_mutex_t *m_handle = data;
    assert_true(NULL != m_handle);
    assert_true(sizeof(simply_thread_mutex_t) == data_size);
    simply_thread_sleep_ms(40);
    assert_true(simply_thread_mutex_lock(mutex_handles[0], 0xFFFFFFFF));
    assert_true(simply_thread_mutex_unlock(mutex_handles[0]));
    assert_true(simply_thread_mutex_lock(mutex_handles[1], 0xFFFFFFFF));
    assert_true(simply_thread_mutex_unlock(mutex_handles[1]));
    simply_thread_sleep_ms(50);
    while(1)
    {
        assert_true(simply_thread_mutex_lock(m_handle[0], 0xFFFFFFFF));
        assert_false(simply_thread_mutex_lock(m_handle[0], 10));
        thread_two_ran = true;
        simply_thread_sleep_ms(25);
        assert_true(simply_thread_mutex_unlock(m_handle[0]));
        simply_thread_sleep_ms(100);
    }
}

static void mutex_worker_3_task(void *data, uint16_t data_size)
{
    assert_true(NULL == data);
    assert_true(0 == data_size);
    assert_true(simply_thread_mutex_lock(mutex_handles[0], 0xFFFFFFFF));
    assert_true(simply_thread_mutex_unlock(mutex_handles[0]));
    assert_true(simply_thread_mutex_lock(mutex_handles[1], 0xFFFFFFFF));
    assert_true(simply_thread_mutex_unlock(mutex_handles[1]));
    while(1)
    {
        simply_thread_sleep_ms(100);
    }
}

static void mutex_test(void **state)
{
    simply_thread_mutex_t mutex_handle;
    thread_one_ran = false;
    thread_two_ran = false;
    simply_thread_reset();
    mutex_handle = simply_thread_mutex_create(NULL);
    assert_true(NULL == mutex_handle);
    assert_false(simply_thread_mutex_unlock(NULL));
    assert_false(simply_thread_mutex_lock(NULL, 0));
    mutex_handle = simply_thread_mutex_create("test_mutex");
    assert_true(NULL != mutex_handle);
    mutex_handles[0] = simply_thread_mutex_create("second_mutex");
    mutex_handles[1] = simply_thread_mutex_create("third_mutex");
    assert_true(NULL != mutex_handles[0]);
    assert_true(NULL != mutex_handles[1]);
    assert_true(simply_thread_mutex_lock(mutex_handles[0], 0));
    assert_true(simply_thread_mutex_lock(mutex_handles[1], 0));
    assert_true(simply_thread_mutex_lock(mutex_handle, 0));
    task_one = simply_thread_new_thread("TASK1", mutex_worker_1_task, 1, &mutex_handle, sizeof(mutex_handle));
    task_two = simply_thread_new_thread("TASK2", mutex_worker_2_task, 3, &mutex_handle, sizeof(mutex_handle));
    assert(NULL != simply_thread_new_thread("TASK3", mutex_worker_3_task, 4, NULL, 0));
    assert_false(simply_thread_mutex_lock(mutex_handle, 0xFFFFFFFF));
    simply_thread_sleep_ms(100);
    assert_true(simply_thread_mutex_unlock(mutex_handle));
    assert_true(simply_thread_mutex_unlock(mutex_handles[0]));
    assert_true(simply_thread_mutex_unlock(mutex_handles[1]));
    assert_true(NULL != task_one);
    assert_true(NULL != task_two);
    simply_thread_sleep_ms(650);
    assert(true == thread_one_ran);
    assert(true == thread_two_ran);
    simply_thread_cleanup();
}

static void first_mutex_test_tests(void **state)
{
    mutex_test(state);
}

static void second_mutex_test_tests(void **state)
{
    mutex_test(state);
}

/*********************************************************************
 *********************** Queue Test Items ****************************
 ********************************************************************/

static void first_queue_task(void *data, uint16_t data_size)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);

    unsigned int val = 1;
    //Test the timeout condition
    PRINT_MSG("%s sending to Queue %u\r\n", __FUNCTION__, 0);
    assert(false == simply_thread_queue_send(queue_handles[0], &val, 15));
    PRINT_MSG("%s Receiving on queue %u\r\n", __FUNCTION__, 1);
    assert(true == simply_thread_queue_rcv(queue_handles[1], &val, 1000));
    PRINT_MSG("%s received %u\r\n", __FUNCTION__, val);
    PRINT_MSG("%s Receiving on queue %u\r\n", __FUNCTION__, 0);
    assert_true(simply_thread_queue_rcv(queue_handles[0], &val, 500));
    PRINT_MSG("%s received %u\r\n", __FUNCTION__, val);
    assert(7 == val);
    while(1)
    {
        thread_one_ran = true;
        val = 1;
        PRINT_MSG("%s sending 1\r\n", __FUNCTION__);
        PRINT_MSG("%s sending to Queue %u\r\n", __FUNCTION__, 1);
        assert_true(simply_thread_queue_send(queue_handles[1], &val, 500));
    }
}

static void second_queue_task(void *data, uint16_t data_size)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    simply_thread_sleep_ms(300);
    unsigned int val;
    while(1)
    {
        thread_two_ran = true;
        PRINT_MSG("%s Receiving on queue %u\r\n", __FUNCTION__, 1);
        assert_true(simply_thread_queue_rcv(queue_handles[1], &val, 0xFFFFFFFF));
        PRINT_MSG("%s received %u\r\n", __FUNCTION__, val);
        assert_int_equal(1, val);
    }
}

static void queue_test(void **state)
{
    unsigned int val = 7;
    thread_one_ran = false;
    thread_two_ran = false;
    simply_thread_reset();

    assert_true(NULL == simply_thread_queue_create(NULL, 1, sizeof(unsigned int)));
    assert_true(NULL == simply_thread_queue_create("test", 0, sizeof(unsigned int)));
    assert_true(NULL == simply_thread_queue_create("test", 5, 0));

    queue_handles[0] = simply_thread_queue_create("Queue1", 3, sizeof(unsigned int));
    queue_handles[1] = simply_thread_queue_create("Queue2", 1, sizeof(unsigned int));
    assert_true(NULL != queue_handles[0]);
    assert_true(NULL != queue_handles[1]);

    assert_false(simply_thread_queue_rcv(queue_handles[0], &val, 5));
    assert_false(simply_thread_queue_rcv(NULL, &val, 5));
    assert_false(simply_thread_queue_send(NULL, &val, 0));

    assert_true(simply_thread_queue_send(queue_handles[0], &val, 0));
    val++;
    assert_true(simply_thread_queue_send(queue_handles[0], &val, 0));
    val++;
    assert_true(simply_thread_queue_send(queue_handles[0], &val, 0));
    assert_false(simply_thread_queue_send(queue_handles[0], &val, 0));
    assert_int_equal(3, simply_thread_queue_get_count(queue_handles[0]));
    assert_int_equal(0, simply_thread_queue_get_count(queue_handles[1]));

    PRINT_MSG("Launching the Tasks\r\n");

    assert_true(NULL != simply_thread_new_thread("TASK1", first_queue_task, 4, NULL, 0));
    assert_true(NULL != simply_thread_new_thread("TASK2", second_queue_task, 3, NULL, 0));
    val = 6;
    simply_thread_sleep_ms(100);
    PRINT_MSG("%s sending to Queue %u\r\n", __FUNCTION__, 1);
    assert(true == simply_thread_queue_send(queue_handles[1], &val, 0));
    PRINT_MSG("Waiting for Cleanup\r\n");
    simply_thread_sleep_ms(2000);
    PRINT_MSG("%s shutting down test\r\n", __FUNCTION__);
    simply_thread_cleanup();
    assert_true(thread_one_ran);
    assert_true(thread_two_ran);
}

static void first_queue_test_tests(void **state)
{
    queue_test(state);
}

static void second_queue_test_tests(void **state)
{
    queue_test(state);
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
        cmocka_unit_test(second_timer_tests),
        cmocka_unit_test(first_mutex_test_tests),
        cmocka_unit_test(second_mutex_test_tests),
        cmocka_unit_test(first_queue_test_tests),
        cmocka_unit_test(second_queue_test_tests),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}

