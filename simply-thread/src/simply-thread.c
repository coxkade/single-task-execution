/**
 * @file simply-thread.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 *
 */

#include <simply-thread-objects.h>
#include <simply-thread.h>
#include <priv-simply-thread.h>
#include <simply-thread-log.h>
#include <simply-thread-linked-list.h>
#include <simply-thread-scheduler.h>
#include <simply-thread-timers.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_MAGENTA, __VA_ARGS__)
#define ROOT_PRINT(...) simply_thread_log(COLOR_YELLOW, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#define ROOT_PRINT(...)
#endif //DEBUG_SIMPLY_THREAD

#ifndef ST_NS_PER_MS
#define ST_NS_PER_MS 1E6
#endif //ST_NS_PER_MS

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define MUTEX_GET() do{\
PRINT_MSG("**** %s waiting on Master Mutex\r\n", __FUNCTION__);\
assert(0 == pthread_mutex_lock(&m_module_data.master_mutex));\
PRINT_MSG("++++ %s Has Master Mutex\r\n", __FUNCTION__);\
}while(0)
#define MUTEX_RELEASE() do{\
pthread_mutex_unlock(&m_module_data.master_mutex);\
PRINT_MSG("---- %s released master mutex\r\n", __FUNCTION__);\
}while(0)

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/



/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/**
 * @brief Function that sleeps for the specified number of milliseconds
 * @param ms The number of milliseconds to sleep
 */
void m_simply_thread_sleep_ms(unsigned long ms);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct simply_thread_lib_data_s m_module_data =
{
    .master_mutex = PTHREAD_MUTEX_INITIALIZER,
    .thread_list = NULL,
    .sleep = {
        .kill_thread = false,
        .mutex = PTHREAD_MUTEX_INITIALIZER
    },
    .signals_initialized = false,
    .print_mutex = PTHREAD_MUTEX_INITIALIZER,
    .cleaning_up = false
}; //!< Local Data for the module

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief Function that spins until a task is set to the running state
 * @param task
 */
static void m_task_wait_running(struct simply_thread_task_s *task)
{
    assert(NULL != task);
    PRINT_MSG("%s Paused\r\n", task->name);
    static const unsigned int wait_time = ST_NS_PER_MS / 1000 + 1;
    while(SIMPLY_THREAD_TASK_RUNNING != task->state)
    {
        simply_thread_sleep_ns(wait_time);
    }
    PRINT_MSG("%s Resumed\r\n", task->name);
}

/**
 * @brief get a pointer to the task calling this function
 * @return NULL on error otherwise a task pointer
 */
static struct simply_thread_task_s *m_get_ex_task(void)
{
    pthread_t pthread;
    pthread = pthread_self();
    struct simply_thread_task_s *rv;
    rv = NULL;
    for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.thread_list) && NULL == rv; i++)
    {
        rv = (struct simply_thread_task_s *)simply_thread_ll_get(m_module_data.thread_list, i);
        if(NULL != rv)
        {
            if(pthread != rv->thread)
            {
                rv = NULL;
            }
        }
    }
    return rv;
}

/**
 * Function that causes a task to spin until its state is set to running
 * @param signo
 */
static void m_usr1_catch(int signo)
{
    struct simply_thread_task_s *ptr_task;
    assert(SIGUSR1 == signo);
    MUTEX_GET();
    ptr_task = m_get_ex_task();
    if(NULL != ptr_task)
    {
        ptr_task->state = SIMPLY_THREAD_TASK_READY;
        simply_thread_tell_sched_task_sleeping(ptr_task);
    }
    MUTEX_RELEASE();
    m_task_wait_running(ptr_task);
}

/**
 * Function that causes a task to return NULL
 * @param signo
 */
static void m_usr2_catch(int signo)
{
    struct simply_thread_task_s *ptr_task;
    assert(SIGUSR2 == signo);
    MUTEX_GET();
    ptr_task = m_get_ex_task();
    MUTEX_RELEASE();
    assert(NULL != ptr_task);
    PRINT_MSG("\tForce Closing %s\r\n", ptr_task->name);
    pthread_exit(NULL);
}


/**
 * @brief the main task wrapper
 * @param data
 */
static void *m_task_wrapper(void *data)
{
    struct simply_thread_task_s *typed;
    typed = data;
    assert(NULL != typed);
    typed->started = true;
    m_task_wait_running(typed);
    typed->fnct(typed->task_data.data, typed->task_data.data_size);
    return NULL;
}

/**
 * @brief Internal cleanup function
 */
static void m_intern_cleanup(void)
{
    static bool first_time = true;
    struct simply_thread_task_s *c;
    m_module_data.cleaning_up = true;
    if(false == first_time)
    {
        simply_thread_timers_destroy();
        MUTEX_RELEASE();
        simply_thread_scheduler_kill();
        MUTEX_GET();
        m_module_data.sleep.kill_thread = true;
        MUTEX_RELEASE();
        pthread_join(m_module_data.sleep.thread, NULL);
        MUTEX_GET();
        m_module_data.sleep.kill_thread = false;
        if(NULL != m_module_data.sleep.sleep_list)
        {
            simply_thread_ll_destroy(m_module_data.sleep.sleep_list);
            m_module_data.sleep.sleep_list = NULL;
        }
        if(NULL != m_module_data.thread_list)
        {
            for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.thread_list); i++)
            {
                c = (struct simply_thread_task_s *)simply_thread_ll_get(m_module_data.thread_list, i);
                assert(c != NULL);
                assert(0 == pthread_kill(c->thread, SIGUSR2));
                MUTEX_RELEASE();
                pthread_join(c->thread, NULL);
                MUTEX_GET();
            }
            simply_thread_ll_destroy(m_module_data.thread_list);
            m_module_data.thread_list = NULL;
        }
    }
    m_module_data.cleaning_up = false;
    first_time = false;
}

/**
 * @brief function that handles the sleeper data
 * @param UNUSED
 */
void *m_sleeper_task(void *data)
{
    struct simply_thread_sleeper_data_s *c_data;
    struct simply_thread_scheduler_data_s sched_data;
    bool timeout;
    bool schedrequired;
    bool task_ready = false;
    sched_data.sleeprequired = true;
    sched_data.task_adjust = NULL;
    PRINT_MSG("Sleeper Manager Started\r\n");
    while(1)
    {
        schedrequired = false;
        task_ready = false;
        assert(0 == pthread_mutex_lock(&m_module_data.sleep.mutex));
        if(true == m_module_data.sleep.kill_thread)
        {
            pthread_mutex_unlock(&m_module_data.sleep.mutex);
            PRINT_MSG("Sleeper Manager Exiting\r\n");
            return NULL;
        }
        //Increment all of the sleeping task counts
        for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.sleep.sleep_list); i++)
        {
            c_data = simply_thread_ll_get(m_module_data.sleep.sleep_list, i);
            assert(NULL != c_data);
            c_data->current_ms++;
            if(c_data->current_ms >= c_data->ms)
            {
                task_ready = true;
            }
        }
        if(true == task_ready)
        {
            MUTEX_GET();
            do
            {
                timeout = false;
                for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.sleep.sleep_list) && false == timeout; i++)
                {
                    c_data = simply_thread_ll_get(m_module_data.sleep.sleep_list, i);
                    assert(NULL != c_data);
                    if(c_data->current_ms >= c_data->ms)
                    {
                        assert(NULL != c_data->task_adjust);
                        PRINT_MSG("\tTask %s Ready From Timer\r\n", c_data->task_adjust->name);
                        if(c_data->task_adjust->state == SIMPLY_THREAD_TASK_SUSPENDED)
                        {
                            c_data->task_adjust->state = SIMPLY_THREAD_TASK_READY;
                        }
                        timeout = true;
                        schedrequired = true;
                        simply_thread_ll_remove(m_module_data.sleep.sleep_list, i);
                        PRINT_MSG("\tIndex %d removed\r\n", i);
                    }
                }
            }
            while(true == timeout);
            MUTEX_RELEASE();
        }
        pthread_mutex_unlock(&m_module_data.sleep.mutex);
        if(true == schedrequired)
        {
            PRINT_MSG("\tTimer Requires the scheduler to run\r\n");
            simply_thread_run(&sched_data);
        }

        m_simply_thread_sleep_ms(1);
    }
}

/**
 * Function that resets the simply thread library.  Closes all existing created threads
 */
void simply_thread_reset(void)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    simply_thread_ll_test();
    MUTEX_GET();
    if(false == m_module_data.signals_initialized)
    {
        signal(SIGUSR1, m_usr1_catch);
        signal(SIGUSR2, m_usr2_catch);
        m_module_data.signals_initialized = true;
    }
    m_intern_cleanup();
    //Re initialize the timers module
    simply_thread_timers_init();
    //recreate sleep list
    m_module_data.sleep.sleep_list = simply_thread_new_ll(sizeof(struct simply_thread_sleeper_data_s));
    assert(NULL != m_module_data.sleep.sleep_list);
    //Recreate thread list
    m_module_data.thread_list = simply_thread_new_ll(sizeof(struct simply_thread_task_s));
    assert(NULL != m_module_data.thread_list);
    simply_thread_scheduler_init();
    assert(0 == pthread_create(&m_module_data.sleep.thread, NULL, m_sleeper_task, NULL));
    MUTEX_RELEASE();
}

/**
 * @brief cleanup the simply thread module;  Will kill all running tasks
 */
void simply_thread_cleanup(void)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    MUTEX_GET();
    m_intern_cleanup();
    MUTEX_RELEASE();
}

/**
 * @brief Function that creates a new thread
 * @param name The name of the thread
 * @param cb the worker function of the thread
 * @param priority the priority of the thread
 * @param data the data to pass to the thread
 * @param data_size the size of the data to pass to the thread
 * @return handle of the new thread
 */
simply_thread_task_t simply_thread_new_thread(const char *name, simply_thread_task_fnct cb, unsigned int priority, void *data, uint16_t data_size)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    struct simply_thread_task_s task;
    struct simply_thread_task_s *ptr_task;
    task.abort = false;
    task.priority = priority;
    task.state = SIMPLY_THREAD_TASK_SUSPENDED;
    task.task_data.data = NULL;
    task.task_data.data_size = 0;
    task.fnct = cb;
    task.name = name;
    task.started = false;
    assert(NULL != name && NULL != cb);
    if(data_size != 0)
    {
        assert(NULL != data);
        task.task_data.data_size = data_size;
        task.task_data.data = malloc(data_size);
        assert(NULL != task.task_data.data);
        memcpy(task.task_data.data, data, data_size);
    }
    MUTEX_GET();
    assert(true == simply_thread_ll_append(m_module_data.thread_list, &task));
    ptr_task = (struct simply_thread_task_s *)simply_thread_ll_get_final(m_module_data.thread_list);
    assert(NULL != ptr_task);
    assert(0 == memcmp(&task, ptr_task, sizeof(task)));
    //Ok now launch the thread
    assert(0 == pthread_create(&ptr_task->thread, NULL, m_task_wrapper, ptr_task));
    //wait for the task to start
    while(false == ptr_task->started)
    {
        simply_thread_sleep_ns(10);
    }
    MUTEX_RELEASE();
    simply_thread_set_task_state(ptr_task, SIMPLY_THREAD_TASK_READY);
    return (simply_thread_task_t)ptr_task;
}

/**
 * @brief Function that sleeps for the specified number of nanoseconds
 * @param ns number of nanoseconds to sleep
 */
void simply_thread_sleep_ns(unsigned long ns)
{
    struct timespec time_data =
    {
        .tv_sec = 0,
        .tv_nsec = ns
    };

    while(0 != nanosleep(&time_data, &time_data))
    {
        if(true == m_module_data.cleaning_up)
        {
            PRINT_MSG("Cleaning up.  Bail on sleep\r\n");
            return;
        }
    }
}


/**
 * @brief Function that sleeps for the specified number of milliseconds
 * @param ms The number of milliseconds to sleep
 */
void m_simply_thread_sleep_ms(unsigned long ms)
{
    static const unsigned long ns_in_ms = ST_NS_PER_MS;
    //Sleep 1 ms at a time
    for(unsigned long i = 0; i < ms; i++)
    {
        simply_thread_sleep_ns(ns_in_ms);
    }
}



/**
 * @brief Function that sleeps for the specified number of milliseconds
 * @param ms The number of milliseconds to sleep
 */
void simply_thread_sleep_ms(unsigned long ms)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    struct simply_thread_task_s *ptr_task;
    struct simply_thread_sleeper_data_s sleep_data;
    MUTEX_GET();
    ptr_task = m_get_ex_task();
    if(NULL == ptr_task)
    {
        ROOT_PRINT("\tptr_task is NULL\r\n");
        MUTEX_RELEASE();
        m_simply_thread_sleep_ms(ms);
    }
    else
    {
        //Trigger a sleep wait
        ROOT_PRINT("\tTrigger sleep wait is not NULL\r\n");
        assert(0 == pthread_mutex_lock(&m_module_data.sleep.mutex));
        sleep_data.current_ms = 0;
        sleep_data.ms = ms;
        sleep_data.task_adjust = ptr_task;
        assert(true == simply_thread_ll_append(m_module_data.sleep.sleep_list, &sleep_data));
        pthread_mutex_unlock(&m_module_data.sleep.mutex);
        MUTEX_RELEASE();
        simply_thread_set_task_state(ptr_task, SIMPLY_THREAD_TASK_SUSPENDED);
    }
}


/**
 * @brief Update a tasks state
 * @param task the task to update
 * @param state the state to update the task to
 */
void simply_thread_set_task_state(struct simply_thread_task_s *task, enum simply_thread_thread_state_e state)
{
    struct simply_thread_scheduler_data_s sched_data;
    struct simply_thread_task_s *c_task;

    sched_data.sleeprequired = false;
    MUTEX_GET();
    c_task =  m_get_ex_task();
    if(NULL == c_task)
    {
        sched_data.sleeprequired = true;
    }
    else
    {
        assert(SIMPLY_THREAD_TASK_RUNNING == c_task->state);
        c_task->state = SIMPLY_THREAD_TASK_READY;
    }
    if(task != c_task)
    {
        sched_data.new_state = state;
        sched_data.task_adjust = task;
    }
    else
    {
        //We are setting our own state, As such we are responsible for suspending ourself
        assert(SIMPLY_THREAD_TASK_RUNNING != state && SIMPLY_THREAD_TASK_READY != state);
        task->state = state;
        sched_data.task_adjust = NULL;
    }
    MUTEX_RELEASE();
    simply_thread_run(&sched_data);
    if(NULL != c_task)
    {
        m_task_wait_running(c_task);
    }
}

/**
 * @brief Function that suspends a task
 * @param handle
 * @return true on success
 */
bool simply_thread_task_suspend(simply_thread_task_t handle)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    struct simply_thread_task_s *ptr_task = (struct simply_thread_task_s *)handle;
    MUTEX_GET();
    if(NULL == ptr_task)
    {
        ptr_task = m_get_ex_task();
    }
    MUTEX_RELEASE();
    if(NULL == ptr_task)
    {
        return false;
    }
    simply_thread_set_task_state(ptr_task, SIMPLY_THREAD_TASK_SUSPENDED);
    return true;
}

/**
 * @brief Function that resumes a task
 * @param handle
 * @return true on success
 */
bool simply_thread_task_resume(simply_thread_task_t handle)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    struct simply_thread_task_s *ptr_task = (struct simply_thread_task_s *)handle;
    if(NULL == handle)
    {
        return false;
    }
    simply_thread_set_task_state(ptr_task, SIMPLY_THREAD_TASK_READY);
    return true;
}


/**
 * @brief initialize a condition
 * @param cond
 */
void simply_thread_init_condition(struct simply_thread_condition_s *cond)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    pthread_mutexattr_t attr;
    assert(0 == pthread_mutexattr_init(&attr));
    assert(0 == pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
    assert(0 == pthread_mutex_init(&cond->gate_mutex, &attr));
    cond->waiting = false;
    PRINT_MSG("\tCondition %p initialized\r\n", cond);
}

/**
 * @brief Destroy a condition
 * @param cond
 */
void simply_thread_dest_condition(struct simply_thread_condition_s *cond)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    pthread_mutex_unlock(&cond->gate_mutex);
    assert(0 == pthread_mutex_destroy(&cond->gate_mutex));
    PRINT_MSG("\tCondition %p destroyed\r\n", cond);
}

/**
 * @brief send a condition
 * @param cond
 */
void simply_thread_send_condition(struct simply_thread_condition_s *cond)
{
    PRINT_MSG("%s: %p\r\n", __FUNCTION__, cond);
    bool ready = false;
    do
    {
        assert(0 == pthread_mutex_lock(&cond->gate_mutex));
        ready = cond->waiting;
        if(false == ready)
        {
            pthread_mutex_unlock(&cond->gate_mutex);
        }
    }
    while(false == ready);
    assert(true == cond->waiting);
    cond->waiting = false;
    pthread_mutex_unlock(&cond->gate_mutex);
}

/**
 * @brief wait on a condition
 * @param cond
 */
void simply_thread_wait_condition(struct simply_thread_condition_s *cond)
{
    bool ready = false;
    PRINT_MSG("%s\r\n", __FUNCTION__);
    PRINT_MSG("\tCondition %p waiting\r\n", cond);
    assert(0 == pthread_mutex_lock(&cond->gate_mutex));
    cond->waiting = true;
    pthread_mutex_unlock(&cond->gate_mutex);
    do
    {
        assert(0 == pthread_mutex_lock(&cond->gate_mutex));
        if(false == cond->waiting)
        {
            ready = true;
        }
        pthread_mutex_unlock(&cond->gate_mutex);
    }
    while(false == ready);
    PRINT_MSG("\tCondition Wait complete %p\r\n", cond);
}

/**
 * @brief Function that fetches the simply thread library data
 * @return pointer to the library data
 */
struct simply_thread_lib_data_s *simply_thread_lib_data(void)
{
    return &m_module_data;
}
