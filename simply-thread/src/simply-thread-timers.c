/**
 * @file simply-thread-timers.c
 * @author Kade Cox
 * @date Created: Jan 20, 2020
 * @details
 * module that manages timers created by the simply thread library
 */

#include <simply-thread-timers.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <simply-thread-linked-list.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdbool.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

//Macro that casts the handle data
#define TIMER_DATA(x) ((struct single_timer_data_s *)x)

#ifndef ST_NS_PER_MS
#define ST_NS_PER_MS 1E6
#endif //ST_NS_PER_MS

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_WHITE, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct single_timer_data_s
{
    pthread_mutex_t timer_mutex; //!<< Mutex that protects the timer
    simply_thread_timer_cb callback; //!< Callback function to trigger with the timer
    const char *name;  //!< The name of the timer
    unsigned int period; //!< The timer period in milliseconds
    simply_thread_timer_type_e mode; //!< The Timers runtime mode
    bool kill; //!< Boolean value to tell the timer to bail
    pthread_t thread; //!< The pthread executing this timer
    simply_thread_timer_t handle; //!< The handle to this timer
    bool running; //!< Boolean that tells if the timer is running
};

struct timer_module_data_s
{
    simply_thread_linked_list_t timer_list; //!< Handle that contains the list of timer handles
    pthread_mutex_t mutex; //!< mutex to protect the list data
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct timer_module_data_s module_data =
{
    .timer_list = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER
}; //!< Local module data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


/**
 * @brief the timer worker for the simply thread timer
 * @param data
 */
static void *priv_simply_thread_timer_worker(void *data)
{
    assert(NULL != data);
    unsigned int count = 0;
    simply_thread_timer_cb runner;
    PRINT_MSG("%s for timer %s starting\r\n", __FUNCTION__, TIMER_DATA(data)->name);
    while(false == TIMER_DATA(data)->kill)
    {
        runner = NULL;
        assert(0 == pthread_mutex_lock(&TIMER_DATA(data)->timer_mutex));
        if(true == TIMER_DATA(data)->running)
        {
            if(count >= TIMER_DATA(data)->period)
            {
                runner = TIMER_DATA(data)->callback;
                if(TIMER_DATA(data)->mode == SIMPLY_THREAD_TIMER_ONE_SHOT)
                {
                    TIMER_DATA(data)->running = false;
                }
            }
        }
        assert(0 == pthread_mutex_unlock(&TIMER_DATA(data)->timer_mutex));
        count++;
        if(NULL != runner)
        {
            count = 0;
            runner(TIMER_DATA(data)->handle);
        }
        simply_thread_sleep_ns(ST_NS_PER_MS);
    }
    PRINT_MSG("%s for timer %s returning\r\n", __FUNCTION__, TIMER_DATA(data)->name);
    return NULL;
}

/**
 * @brief Set up the simply thread timers
 */
void simply_thread_timers_init(void)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    assert(0 == pthread_mutex_lock(&module_data.mutex));
    if(NULL == module_data.timer_list)
    {
        module_data.timer_list = simply_thread_new_ll(sizeof(simply_thread_timer_t));
        assert(NULL != module_data.timer_list);
    }
    assert(0 == pthread_mutex_unlock(&module_data.mutex));
}

/**
 * @brief Destroy all simply thread timers
 */
void simply_thread_timers_destroy(void)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    assert(0 == pthread_mutex_lock(&module_data.mutex));
    simply_thread_timer_t *timer_handle;
    if(NULL != module_data.timer_list)
    {
        for(unsigned int i = 0; i < simply_thread_ll_count(module_data.timer_list); i++)
        {
            timer_handle = simply_thread_ll_get(module_data.timer_list, i);
            assert(NULL != timer_handle);
            PRINT_MSG("\tHandling timer: %p\r\n", timer_handle[0]);
            assert(0 == pthread_mutex_lock(&TIMER_DATA(timer_handle[0])->timer_mutex));
            TIMER_DATA(timer_handle[0])->kill = true;
            assert(0 == pthread_mutex_unlock(&TIMER_DATA(timer_handle[0])->timer_mutex));
            pthread_join(TIMER_DATA(timer_handle[0])->thread, NULL);
            free(TIMER_DATA(timer_handle[0]));
        }
        simply_thread_ll_destroy(module_data.timer_list);
        module_data.timer_list = NULL;
    }
    assert(0 == pthread_mutex_unlock(&module_data.mutex));
}

/**
 * @brief Function that creates a new timer
 * @param cb callback function to trigger when time elapses
 * @param name the name of the timer
 * @param period_ms the period in milliseconds
 * @param mode the mode of the timer, repeat etc.
 * @param run_now if true start the timer now
 * @return handle of the new timer, NULL on error
 */
simply_thread_timer_t simply_thread_create_timer(simply_thread_timer_cb cb, const char *name, unsigned int period_ms, simply_thread_timer_type_e mode,
        bool run_now)
{
    simply_thread_timer_t rv = NULL;
    pthread_mutexattr_t attr;
    struct single_timer_data_s *new_timer;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    assert(0 == pthread_mutex_lock(&module_data.mutex));
    if(NULL == module_data.timer_list || NULL == cb || NULL == name || 0 == period_ms)
    {
        assert(0 == pthread_mutex_unlock(&module_data.mutex));
        return rv;
    }
    new_timer = malloc(sizeof(struct single_timer_data_s));
    assert(NULL != new_timer);
    rv = new_timer;
    new_timer->callback = cb;
    new_timer->name = name;
    new_timer->period = period_ms;
    new_timer->handle = rv;
    new_timer->mode = mode;
    new_timer->kill = false;
    new_timer->running = false;

    //Initialize and lock the mutex
    assert(0 == pthread_mutexattr_init(&attr));
    assert(0 == pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
    assert(0 == pthread_mutex_init(&new_timer->timer_mutex, &attr));
    assert(0 == pthread_mutex_lock(&new_timer->timer_mutex));

    //Initialize the timers thread
    assert(0 == pthread_create(&new_timer->thread, NULL, priv_simply_thread_timer_worker, (void *)rv));
    simply_thread_ll_append(module_data.timer_list, &rv);
    PRINT_MSG("\tCreated Timer: %p\r\n", rv);
    assert(0 == pthread_mutex_unlock(&module_data.mutex));
    assert(0 == pthread_mutex_unlock(&new_timer->timer_mutex));
    if(true == run_now)
    {
        assert(true == simply_thread_timer_start(rv));
    }
    return rv;
}

/**
 * @brief Function that starts a simply thread timer
 * @param timer the handle of the timer to start
 * @return true on success
 */
bool simply_thread_timer_start(simply_thread_timer_t timer)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    if(NULL == timer)
    {
        return false;
    }
    assert(0 == pthread_mutex_lock(&module_data.mutex));
    assert(0 == pthread_mutex_lock(&TIMER_DATA(timer)->timer_mutex));
    TIMER_DATA(timer)->running = true;
    assert(0 == pthread_mutex_unlock(&TIMER_DATA(timer)->timer_mutex));
    assert(0 == pthread_mutex_unlock(&module_data.mutex));
    return true;
}

/**
 * @brief Function that stops a simply thread timer
 * @param timer the handle of the timer to stop
 * @return true on success
 */
bool simply_thread_timer_stop(simply_thread_timer_t timer)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    if(NULL == timer)
    {
        return false;
    }
    assert(0 == pthread_mutex_lock(&module_data.mutex));
    assert(0 == pthread_mutex_lock(&TIMER_DATA(timer)->timer_mutex));
    TIMER_DATA(timer)->running = false;
    assert(0 == pthread_mutex_unlock(&TIMER_DATA(timer)->timer_mutex));
    assert(0 == pthread_mutex_unlock(&module_data.mutex));
    return true;
}
