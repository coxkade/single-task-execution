/**
 * @file simply-thread.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 *
 */

#include <simply-thread.h>
#include <priv-simply-thread.h>
#include <simply-thread-log.h>
#include <simply-thread-objects.h>
#include <simply-thread-linked-list.h>
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
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define MUTEX_GET() assert(0 == pthread_mutex_lock(&m_module_data.master_mutex))
#define MUTEX_RELEASE() pthread_mutex_unlock(&m_module_data.master_mutex)

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/


struct module_data_s
{
    pthread_mutex_t master_mutex; //!< The modules master mutex
    pthread_mutex_t sched_mutex; //!< The schedulers mutex
    simply_thread_linked_list_t thread_list; //!< The thread list
    bool signals_initialized; //!< Tells if the signals have been initialized
};

struct module_scheduler_data_s
{
    struct simply_thread_task_s *task_adjust;
    enum simply_thread_thread_state_e new_state;
}; //!< Structure holding data for the scheduler to use

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct module_data_s m_module_data =
{
    .master_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sched_mutex = PTHREAD_MUTEX_INITIALIZER,
    .thread_list = NULL,
    .signals_initialized = false
};

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
        simply_thread_sleep_ns(1000);
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
    assert(NULL != ptr_task);
    ptr_task->state = SIMPLY_THREAD_TASK_READY;
    MUTEX_RELEASE();
    m_task_wait_running(ptr_task);
}

/**
 * @brief function that fetches the number of running tasks
 * @return the number of running tasks
 */
static inline unsigned int m_running_tasks(void)
{
    struct simply_thread_task_s *c;
    unsigned int rv = 0;
    for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.thread_list); i++)
    {
        c = (struct simply_thread_task_s *)simply_thread_ll_get(m_module_data.thread_list, i);
        if(SIMPLY_THREAD_TASK_RUNNING == c->state)
        {
            rv++;
        }
    }
    assert(2 > rv);
    return rv;
}

/**
 * @brief Function that sleeps all running tasks
 */
static inline void m_sleep_all_tasks(void)
{
    struct simply_thread_task_s *c;

    while(0 != m_running_tasks())
    {
        for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.thread_list); i++)
        {
            c = (struct simply_thread_task_s *)simply_thread_ll_get(m_module_data.thread_list, i);
            assert(NULL != c);
            if(SIMPLY_THREAD_TASK_RUNNING == c->state)
            {
                assert(0 == pthread_kill(c->thread, SIGUSR1));
            }
        }
        MUTEX_RELEASE();
        simply_thread_sleep_ns(1000);
        MUTEX_GET();
    }

}

/**
 * @brief tell the best task to start
 */
static inline void m_sched_run_best_task(void)
{
    struct simply_thread_task_s *c;
    struct simply_thread_task_s *best_task;
    best_task = NULL;
    for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.thread_list); i++)
    {
        c = (struct simply_thread_task_s *)simply_thread_ll_get(m_module_data.thread_list, i);
        assert(NULL != c);
        if(SIMPLY_THREAD_TASK_READY == c->state)
        {
            if(NULL == best_task)
            {
                best_task = c;
            }
            else if(best_task->priority < c->priority)
            {
                best_task = c;
            }
        }
    }
    if(NULL != best_task)
    {
        best_task->state = SIMPLY_THREAD_TASK_RUNNING;
    }
}

static void *m_run_sched(void *data)
{
    struct module_scheduler_data_s *typed;
    assert(NULL != data);
    typed = data;

    assert(0 == pthread_mutex_lock(&m_module_data.sched_mutex));
    MUTEX_GET();
    m_sleep_all_tasks();
    if(NULL != typed->task_adjust)
    {
        //All tasks are asleep set the new state
        typed->task_adjust->state = typed->new_state;
    }
    //Now run the next task
    m_sched_run_best_task();
    MUTEX_RELEASE();
    pthread_mutex_unlock(&m_module_data.sched_mutex);
    free(typed);
    return NULL;
}

/**
 * @brief Function that launches the scheduler
 * @param sched_data
 */
static void m_launch_sched(struct module_scheduler_data_s *sched_data)
{
    pthread_t thread;
    struct module_scheduler_data_s *data;
    data = malloc(sizeof(struct module_scheduler_data_s));
    memcpy(data, sched_data, sizeof(struct module_scheduler_data_s));
    assert(0 == pthread_create(&thread, NULL, m_run_sched, data));
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

static void m_intern_cleanup(void)
{
    //TODO Kill all threads
    assert(NULL == m_module_data.thread_list); //assert to remind me to add task bailQ

    simply_thread_ll_destroy(m_module_data.thread_list);
}

/**
 * Function that resets the simply thread library.  Closes all existing created threads
 */
void simply_thread_reset(void)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    MUTEX_GET();
    if(false == m_module_data.signals_initialized)
    {
        signal(SIGUSR1, m_usr1_catch);
        m_module_data.signals_initialized = true;
    }
    m_intern_cleanup();
    //Recreate thread list
    m_module_data.thread_list = simply_thread_new_ll(sizeof(struct simply_thread_task_s));
    assert(NULL != m_module_data.thread_list);
    MUTEX_RELEASE();
}

/**
 * @brief cleanup the simply thread module;  Will kill all running tasks
 */
void simply_thread_cleanup(void)
{
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
 * @param ns
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
    }
}

/**
 * @brief Function that sleeps for the specified number of milliseconds
 * @param ms
 */
void simply_thread_sleep_ms(unsigned long ms)
{
    static const unsigned long ns_in_ms = 1E6;
    //Sleep 1 ms at a time
    for(unsigned long i = 0; i < ms; i++)
    {
        simply_thread_sleep_ns(ns_in_ms);
    }
}


/**
 * @brief Update a tasks state
 * @param task
 * @param state
 */
void simply_thread_set_task_state(struct simply_thread_task_s *task, enum simply_thread_thread_state_e state)
{
    struct module_scheduler_data_s sched_data;

    if(task != m_get_ex_task())
    {
        MUTEX_GET();
        sched_data.new_state = state;
        sched_data.task_adjust = task;
        m_launch_sched(&sched_data);
        MUTEX_RELEASE();
    }
    else
    {
        MUTEX_GET();
        //We are setting our own state, As such we are responsible for suspending ourself
        assert(SIMPLY_THREAD_TASK_RUNNING != state && SIMPLY_THREAD_TASK_READY != state);
        task->state = state;
        sched_data.task_adjust = NULL;
        m_launch_sched(&sched_data);
        MUTEX_RELEASE();
        m_task_wait_running(task);
    }
}

/**
 * @brief Function that suspends a task
 * @param handle
 * @return true on success
 */
bool simply_thread_task_suspend(simply_thread_task_t handle)
{
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
    struct simply_thread_task_s *ptr_task = (struct simply_thread_task_s *)handle;
    if(NULL == handle)
    {
        return false;
    }
    simply_thread_set_task_state(ptr_task, SIMPLY_THREAD_TASK_READY);
    return true;
}
