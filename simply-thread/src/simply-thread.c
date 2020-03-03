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
#include <simply-thread-mutex.h>
#include <simply_thread_system_clock.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>
#include "priv-inc/master-mutex.h"

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_MAGENTA, __VA_ARGS__)
#define ROOT_PRINT(...) simply_thread_log(COLOR_YELLOW, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#define ROOT_PRINT(...)
#endif //DEBUG_SIMPLY_THREAD

#ifdef DEBUG_MASTER_MUTEX
#define MM_PRINT_MSG(...) simply_thread_log(COLOR_BLUE, __VA_ARGS__)
#else
#define MM_PRINT_MSG(...)
#endif //DEBUG_MASTER_MUTEX

#define MM_DEBUG_MESSAGE(...) MM_PRINT_MSG("%s: %s", __FUNCTION__, __VA_ARGS__)

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct sleep_tick_data_s
{
    uint64_t count;
    uint64_t max_count;
    bool posted;
    simply_thread_sem_t sem;
}; //!< Structure to help with the sleep on tick handler

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
    .init_mutex = PTHREAD_MUTEX_INITIALIZER,
    .master_semaphore = {.sem = NULL},
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
    while(SIMPLY_THREAD_TASK_RUNNING != task->state)
    {
        simply_thread_sem_wait(&task->sem);
    }
    PRINT_MSG("%s Resumed\r\n", task->name);
}

/**
 * @brief get a pointer to the task calling this function
 * @return NULL on error otherwise a task pointer
 */
struct simply_thread_task_s *simply_thread_get_ex_task(void)
{
    pthread_t pthread;
    pthread = pthread_self();
    struct simply_thread_task_s *rv;
    rv = NULL;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.tcb_list)  && NULL == rv; i++)
    {
        rv = &m_module_data.tcb_list[i];
        assert(NULL != rv);
        if(pthread != rv->thread || NULL == rv->name)
        {
            rv = NULL;
        }
    }
    return rv;
}

/**
 * @brief Fetch the current task handle
 * @return void*
 */
void *simply_thread_current_task_handle(void)
{
    struct simply_thread_task_s *rv;
    MUTEX_GET();
    rv = simply_thread_get_ex_task();
    MUTEX_RELEASE();
    return (void *) rv;
}

/**
 * Function that causes a task to spin until its state is set to running
 * @param signo
 */
static void m_usr1_catch(int signo)
{
    struct simply_thread_task_s *ptr_task;
    bool wait_required;
    master_mutex_entry_t m_entry;
    bool keep_going = true;
    int rv;
    int error_val;
    assert(SIGUSR1 == signo);

    m_entry = master_mutex_pull();

    MUTEX_GET();
    wait_required = false;
    ptr_task = simply_thread_get_ex_task();
    if(NULL != ptr_task)
    {
        if(SIMPLY_THREAD_TASK_READY != ptr_task->state)
        {
            ptr_task->state = SIMPLY_THREAD_TASK_READY;
            do
            {
                rv = simply_thread_sem_trywait(&ptr_task->sem);
                error_val = errno;
                if(0 == rv)
                {
                    keep_going = false;
                }
                else if(EAGAIN == error_val)
                {
                    keep_going = false;
                }
                else
                {
                    assert(false);
                }

            }
            while(true == keep_going);
            simply_thread_tell_sched_task_sleeping(ptr_task);
            wait_required = true;
        }
    }
    MUTEX_RELEASE();
    if(true == wait_required)
    {
        m_task_wait_running(ptr_task);
    }

    if(NULL != m_entry)
    {
        MUTEX_GET();
        master_mutex_push(m_entry);
        MUTEX_RELEASE();
    }
}

/**
 * Function that causes a task to return NULL
 * @param signo
 */
static void m_usr2_catch(int signo)
{
    struct simply_thread_task_s *ptr_task;
    assert(SIGUSR2 == signo);
    ptr_task = simply_thread_get_ex_task();
    if(NULL != ptr_task)
    {
        PRINT_MSG("\tForce Closing %s\r\n", ptr_task->name);
        simply_thread_sem_destroy(&ptr_task->sem);
    }
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
    ST_LOG_ERROR("Error!!! task %s Exited on its own\r\n", typed->name);
    assert(false);
    return NULL;
}

/**
 * @brief Internal cleanup function
 */
static void m_intern_cleanup(void)
{
    static bool first_time = true;
    m_module_data.cleaning_up = true;
    PRINT_MSG("---- m_intern_cleanup Running\r\n");
    if(false == first_time)
    {
        //Kill all the running tasks
        MUTEX_RELEASE();
        simply_thread_scheduler_kill();
        simply_thread_timers_destroy();
        MUTEX_GET();
        simply_thead_system_clock_reset();
        master_mutex_prep_signal();
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list); i++)
        {
            m_module_data.sleep.sleep_list[i].in_use = false;
        }
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.tcb_list); i++)
        {
            if(NULL != m_module_data.tcb_list[i].name)
            {
                PRINT_MSG("Killing task %s\r\n", m_module_data.tcb_list[i].name);
                assert(0 == pthread_kill(m_module_data.tcb_list[i].thread, SIGUSR2));
                pthread_join(m_module_data.tcb_list[i].thread, NULL);
                PRINT_MSG("Task %s Killed\r\n", m_module_data.tcb_list[i].name);
                m_module_data.tcb_list[i].name = NULL;
                m_module_data.tcb_list[i].state = SIMPLY_THREAD_TASK_UNKNOWN_STATE;
            }
        }
        PRINT_MSG("Starting Mutex Cleanup\r\n");
        simply_thread_mutex_cleanup();
        PRINT_MSG("Starting Queue Cleanup\r\n");
        simply_thread_queue_cleanup();
        // MUTEX_RELEASE();
        PRINT_MSG("Resetting fifo mutex\r\n");
        master_mutex_reset();
        PRINT_MSG("%s Getting the mutex\r\n", __FUNCTION__);
        MUTEX_GET();
        PRINT_MSG("%s Got the Mutex\r\n", __FUNCTION__);
        simply_thead_system_clock_reset();
    }


    m_module_data.cleaning_up = false;
    first_time = false;
    PRINT_MSG("%s Finished\r\n", __FUNCTION__);
    PRINT_MSG("---- m_intern_cleanup Finishing\r\n");
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
        atexit(sem_helper_cleanup);
    }
    m_intern_cleanup();
    //Reinitialize the timers module
    simply_thread_timers_init();
    // Reinitialize the mutex module
    simply_thread_mutex_init();
    //recreate sleep list
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list); i++)
    {
        m_module_data.sleep.sleep_list[i].in_use = false;
    }
    //Recreate thread list
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.tcb_list); i++)
    {
        m_module_data.tcb_list[i].name = NULL;
        m_module_data.tcb_list[i].state = SIMPLY_THREAD_TASK_UNKNOWN_STATE;
    }
    simply_thread_scheduler_init();
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
    struct simply_thread_task_s *ptr_task;

    assert(NULL != name && NULL != cb);

    MUTEX_GET();
    ptr_task = NULL;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.tcb_list) && NULL == ptr_task; i++)
    {
        if(NULL == m_module_data.tcb_list[i].name)
        {
            m_module_data.tcb_list[i].abort = false;
            m_module_data.tcb_list[i].priority = priority;
            m_module_data.tcb_list[i].state = SIMPLY_THREAD_TASK_SUSPENDED;
            m_module_data.tcb_list[i].task_data.data = NULL;
            m_module_data.tcb_list[i].task_data.data_size = 0;
            m_module_data.tcb_list[i].fnct = cb;
            m_module_data.tcb_list[i].name = name;
            m_module_data.tcb_list[i].started = false;
            simply_thread_sem_init(&m_module_data.tcb_list[i].sem);
            if(data_size != 0)
            {
                assert(NULL != data);
                m_module_data.tcb_list[i].task_data.data_size = data_size;
                m_module_data.tcb_list[i].task_data.data =  m_module_data.tcb_list[i].task_data.data_buffer;
                assert(NULL != m_module_data.tcb_list[i].task_data.data);
                memcpy(m_module_data.tcb_list[i].task_data.data, data, data_size);
            }
            ptr_task = &m_module_data.tcb_list[i];
        }
    }
    assert(NULL != ptr_task);
    //Ok now launch the thread
    assert(0 == pthread_create(&ptr_task->thread, NULL, m_task_wrapper, ptr_task));
    //wait for the task to start
    while(false == ptr_task->started)
    {
    }
    simply_thread_set_task_state_from_locked(ptr_task, SIMPLY_THREAD_TASK_READY);
    MUTEX_RELEASE();
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
 * @brief Function that handles sleep data on tick events
 * @param handle
 * @param new_tick
 * @param args
 */
static void interrupt_sleep_on_tick_worker(sys_clock_on_tick_handle_t handle, uint64_t new_tick, void *args)
{
    struct sleep_tick_data_s *typed;
    typed = args;
    assert(NULL != typed && NULL != handle);
    typed->count++;
    if(typed->count >= typed->max_count && false == typed->posted)
    {
        simply_thread_sem_post(&typed->sem);
        typed->posted = true;
    }
}

/**
 * @brief The sleep on tick worker function
 * @param handle
 * @param new_tick
 * @param args
 */
static void sleep_on_tick_worker(sys_clock_on_tick_handle_t handle, uint64_t new_tick, void *args)
{
    unsigned int *i_ptr;
    unsigned int i;
    struct simply_thread_task_s *task;

    i_ptr = args;
    assert(NULL != i_ptr);
    i = i_ptr[0];
    m_module_data.sleep.sleep_list[i].sleep_data.current_ms++;
    if(m_module_data.sleep.sleep_list[i].sleep_data.current_ms >= m_module_data.sleep.sleep_list[i].sleep_data.ms
            && false == m_module_data.sleep.sleep_list[i].sleep_data.finished
            && true == m_module_data.sleep.sleep_list[i].in_use)
    {
        simply_thead_system_clock_disable_on_tick_from_handler(handle);
        MUTEX_GET();
        assert(true == m_module_data.sleep.sleep_list[i].in_use);
        task = m_module_data.sleep.sleep_list[i].sleep_data.task_adjust;
        if(SIMPLY_THREAD_TASK_SUSPENDED == task->state && true == m_module_data.sleep.sleep_list[i].in_use)
        {
            m_module_data.sleep.sleep_list[i].sleep_data.finished = true;
            simply_thread_set_task_state_from_locked(task, SIMPLY_THREAD_TASK_READY);
        }
        MUTEX_RELEASE();
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
    bool index_found;
    unsigned int index = 500;
    unsigned int *i_ptr;
    struct sleep_tick_data_s *sleep_data;
    sys_clock_on_tick_handle_t on_tick_handle;
    MUTEX_GET();
    ptr_task = simply_thread_get_ex_task();
    if(NULL == ptr_task)
    {
        ROOT_PRINT("\tptr_task is NULL\r\n");
        sleep_data = malloc(sizeof(struct sleep_tick_data_s));
        assert(NULL != sleep_data);
        sleep_data->count = 0;
        sleep_data->max_count = (uint64_t)ms;
        sleep_data->posted = false;
        simply_thread_sem_init(&sleep_data->sem);
        assert(0 == simply_thread_sem_trywait(&sleep_data->sem));
        MUTEX_RELEASE();
        on_tick_handle = simply_thead_system_clock_register_on_tick(interrupt_sleep_on_tick_worker, sleep_data);
        simply_thread_sem_wait(&sleep_data->sem);
        simply_thead_system_clock_deregister_on_tick(on_tick_handle);
        simply_thread_sem_destroy(&sleep_data->sem);
        free(sleep_data);
    }
    else
    {
        //Trigger a sleep wait
        ROOT_PRINT("\tTrigger sleep wait is not NULL\r\n");
        index_found = false;
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list) && false == index_found; i++)
        {
            if(false == m_module_data.sleep.sleep_list[i].in_use)
            {
                index_found = true;
                m_module_data.sleep.sleep_list[i].sleep_data.current_ms = 0;
                m_module_data.sleep.sleep_list[i].sleep_data.ms = ms;
                m_module_data.sleep.sleep_list[i].sleep_data.task_adjust = ptr_task;
                m_module_data.sleep.sleep_list[i].sleep_data.finished = false;
                m_module_data.sleep.sleep_list[i].in_use = true;
                index = i;
                i_ptr = malloc(sizeof(unsigned int));
                assert(NULL != i_ptr);
                i_ptr[0] = i;
            }
        }
        assert(true == index_found);
        assert(index < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list));
        MUTEX_RELEASE();
        on_tick_handle = simply_thead_system_clock_register_on_tick(sleep_on_tick_worker, i_ptr);
        simply_thread_set_task_state(ptr_task, SIMPLY_THREAD_TASK_SUSPENDED);
        simply_thead_system_clock_deregister_on_tick(on_tick_handle);
        MUTEX_GET();
        free(i_ptr);
        assert(true == m_module_data.sleep.sleep_list[index].in_use);
        m_module_data.sleep.sleep_list[index].in_use = false;
        MUTEX_RELEASE();
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
    c_task =  simply_thread_get_ex_task();
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
 * @brief Update a tasks state
 * @param task
 * @param state
 */
void simply_thread_set_task_state_from_locked(struct simply_thread_task_s *task, enum simply_thread_thread_state_e state)
{
    struct simply_thread_scheduler_data_s sched_data;
    struct simply_thread_task_s *c_task;

    sched_data.sleeprequired = false;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    c_task =  simply_thread_get_ex_task();
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
    MUTEX_GET();
}

/**
 * @brief execute the scheduler from a locked context
 */
void simply_ex_sched_from_locked(void)
{
    struct simply_thread_task_s *c_task;
    struct simply_thread_scheduler_data_s sched_data;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    sched_data.sleeprequired = false;
    c_task =  simply_thread_get_ex_task();
    if(NULL == c_task)
    {
        sched_data.sleeprequired = true;
    }
    else
    {
        assert(SIMPLY_THREAD_TASK_RUNNING == c_task->state);
        c_task->state = SIMPLY_THREAD_TASK_READY;
    }
    sched_data.task_adjust = NULL;
    MUTEX_RELEASE();
    simply_thread_run(&sched_data);
    if(NULL != c_task)
    {
        m_task_wait_running(c_task);
    }
    MUTEX_GET();
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
        ptr_task = simply_thread_get_ex_task();
    }
    if(NULL != ptr_task)
    {
        simply_thread_set_task_state_from_locked(ptr_task, SIMPLY_THREAD_TASK_SUSPENDED);
    }
    MUTEX_RELEASE();
    if(NULL == ptr_task)
    {
        return false;
    }
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
 * @brief Function that gets a tasks state
 * @param handle Handle of the task to get the state
 * @return The state of the task
 */
enum simply_thread_thread_state_e simply_thread_task_state(simply_thread_task_t handle)
{
    enum simply_thread_thread_state_e rv;
    struct simply_thread_task_s *ptr_task = (struct simply_thread_task_s *)handle;
    assert(NULL != ptr_task);
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    MUTEX_GET();
    rv = ptr_task->state;
    MUTEX_RELEASE();
    return rv;
}

/**
 * @brief Function that checks if we are currently in an interrupt
 * @return true currently in the interrupt context.
 * @return false  Not Currently in the interrupt context.
 */
bool simply_thread_in_interrupt(void)
{
    struct simply_thread_task_s *ptr_task;
    MUTEX_GET();
    ptr_task = simply_thread_get_ex_task();
    MUTEX_RELEASE();
    if(NULL == ptr_task)
    {
        return true;
    }
    return false;
}


/**
 * @brief initialize a condition
 * @param cond
 */
void simply_thread_init_condition(struct simply_thread_condition_s *cond)
{
    assert(NULL != cond);
    PRINT_MSG("%s\r\n", __FUNCTION__);
    simply_thread_sem_init(&cond->sig_sem);
    assert(0 == simply_thread_sem_trywait(&cond->sig_sem));
    PRINT_MSG("\tCondition %p initialized\r\n", cond);
}

/**
 * @brief Destroy a condition
 * @param cond
 */
void simply_thread_dest_condition(struct simply_thread_condition_s *cond)
{
    assert(NULL != cond);
    PRINT_MSG("%s\r\n", __FUNCTION__);
    simply_thread_sem_destroy(&cond->sig_sem);
    PRINT_MSG("\tCondition %p destroyed\r\n", cond);
}

/**
 * @brief send a condition
 * @param cond
 */
void simply_thread_send_condition(struct simply_thread_condition_s *cond)
{
    assert(NULL != cond);
    PRINT_MSG("%s: %p\r\n", __FUNCTION__, cond);
    assert(0 == simply_thread_sem_post(&cond->sig_sem));
}

/**
 * @brief wait on a condition
 * @param cond
 */
void simply_thread_wait_condition(struct simply_thread_condition_s *cond)
{
    assert(NULL != cond);
    PRINT_MSG("\tCondition %p waiting\r\n", cond);
    while(0 != simply_thread_sem_wait(&cond->sig_sem)) {}
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

/**
 * @brief Function that gets the master mutex
 * @return true on success
 */
bool simply_thread_get_master_mutex(void)
{
    return master_mutex_get();
}

/**
 * @brief Function that releases the master mutex
 */
void simply_thread_release_master_mutex(void)
{
    master_mutex_release();
}

/**
 * @brief Function that checks if the master mutex is locked
 * @return true if the master mutex is locked
 */
bool simply_thread_master_mutex_locked(void)
{
    return master_mutex_locked();
}


static inline void _print_task_stats(struct simply_thread_task_s *tcb)
{
    assert(NULL != tcb);
    ST_LOG_ERROR("\tNAME: %s\r\n", tcb->name);
    ST_LOG_ERROR("\tPriority: %u\r\n", tcb->priority);
    switch(tcb->state)
    {
        case SIMPLY_THREAD_TASK_RUNNING:
            ST_LOG_ERROR("\tSTATE: SIMPLY_THREAD_TASK_RUNNING\r\n");
            break;
        case SIMPLY_THREAD_TASK_READY:
            ST_LOG_ERROR("\tSTATE: SIMPLY_THREAD_TASK_READY\r\n");
            break;
        case SIMPLY_THREAD_TASK_BLOCKED:
            ST_LOG_ERROR("\tSTATE: SIMPLY_THREAD_TASK_BLOCKED\r\n");
            break;
        case SIMPLY_THREAD_TASK_SUSPENDED:
            ST_LOG_ERROR("\tSTATE: SIMPLY_THREAD_TASK_SUSPENDED\r\n");
            break;
        case SIMPLY_THREAD_TASK_UNKNOWN_STATE:
            ST_LOG_ERROR("\tSTATE: SIMPLY_THREAD_TASK_UNKNOWN_STATE\r\n");
            break;
        case SIMPLY_THREAD__TASK_STATE_COUNT:
            ST_LOG_ERROR("\tSTATE: SIMPLY_THREAD__TASK_STATE_COUNT\r\n");
            break;
        default:
            assert(false);
            break;
    }
    ST_LOG_ERROR("\r\n");
}

/**
 * @brief Function that prints the contents of the tcb
 */
void simply_thread_print_tcb(void)
{
    MUTEX_GET();
    ST_LOG_ERROR("ALL TASK States\r\n")
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.tcb_list); i++)
    {
        if(NULL != m_module_data.tcb_list[i].name)
        {
            _print_task_stats(&m_module_data.tcb_list[i]);
        }
    }
    MUTEX_RELEASE();
}

/**
 * @brief Internal implementation of snprintf
 * @param s The too buffer
 * @param n the max size of the buffer
 * @param format The format of the string
 * @return The size of the string
 */
int simply_thread_snprintf(char *s, size_t n, const char *format, ...)
{
    va_list argList;
    va_list worker;

    assert(NULL != s);
    assert(0 < n);

    int rv = -1;

    va_start(argList, format);

    va_copy(worker, argList);
    assert(n >= vsnprintf(NULL, 0, format, worker));
    va_end(worker);

    va_copy(worker, argList);
    rv = vsnprintf(s, n, format, worker);
    va_end(worker);

    va_end(argList);
    return rv;
}
