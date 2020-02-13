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

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_MAGENTA, __VA_ARGS__)
#define ROOT_PRINT(...) simply_thread_log(COLOR_YELLOW, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#define ROOT_PRINT(...)
#endif //DEBUG_SIMPLY_THREAD

//#define DEBUG_MASTER_MUTEX

#ifdef DEBUG_MASTER_MUTEX
#define MM_PRINT_MSG(...) simply_thread_log(COLOR_BLUE, __VA_ARGS__)
#else
#define MM_PRINT_MSG(...)
#endif //DEBUG_MASTER_MUTEX

#define MM_DEBUG_MESSAGE(...) MM_PRINT_MSG("%s: %s", __FUNCTION__, __VA_ARGS__)

#ifndef ST_NS_PER_MS
#define ST_NS_PER_MS 1E6
#endif //ST_NS_PER_MS

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

/**
 * @brief Function that sleeps for the specified number of milliseconds
 * @param ms The number of milliseconds to sleep
 */
void m_simply_thread_sleep_ms(unsigned long ms);

/**
 * @brief sleep maintenance function
 */
static void m_sleep_maint(void);

/**
 * @brief Function that initializes the master semaphore
 */
static void m_init_master_semaphore(void);

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


static struct {
	void * allocated;
	void * semaphore;
}m_post_info;

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief helper function that gets the threads identifier
 * @return The ID
 */
static uint64_t m_get_thread_id(void)
{
	uint64_t rv;
	pthread_threadid_np(NULL, &rv);
	return rv;
}

/**
 * @brief system maintenance timer
 * @param timer_handle
 */
static void m_maint_timer(simply_thread_timer_t timer_handle)
{
    assert(true == simply_thread_get_master_mutex());
    //Run the sleep maintenance
    m_sleep_maint();
    //trigger the mutex maintenance
    simply_thread_mutex_maint();
    //trigger the queue maintenance
    simply_thread_queue_maint();
    simply_thread_release_master_mutex();
}

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
    bool keep_going = true;
    int rv;
    int error_val;
    assert(SIGUSR1 == signo);
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
    ptr_task = simply_thread_get_ex_task();
    MUTEX_RELEASE();
    assert(NULL != ptr_task);
    PRINT_MSG("\tForce Closing %s\r\n", ptr_task->name);
    simply_thread_sem_destroy(&ptr_task->sem);
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
    if(false == first_time)
    {
        MUTEX_RELEASE();
        simply_thread_scheduler_kill();
        simply_thread_timers_destroy();
        MUTEX_GET();
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list); i++)
        {
            m_module_data.sleep.sleep_list[i].in_use = false;
        }
		for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.tcb_list); i++)
		{
			if(NULL != m_module_data.tcb_list[i].name)
			{
				assert(0 == pthread_kill(m_module_data.tcb_list[i].thread, SIGUSR2));
				MUTEX_RELEASE();
				pthread_join(m_module_data.tcb_list[i].thread, NULL);
				MUTEX_GET();
				m_module_data.tcb_list[i].name = NULL;
                m_module_data.tcb_list[i].state = SIMPLY_THREAD_TASK_UNKNOWN_STATE;
			}
		}
        simply_thread_mutex_cleanup();
        simply_thread_queue_cleanup();
    }
    m_module_data.cleaning_up = false;
    first_time = false;
}

/**
 * @brief sleep maintenance function
 */
static void m_sleep_maint(void)
{
    bool timeout;
    bool task_ready = false;
    task_ready = false;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    //Increment all of the sleeping task counts
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list); i++)
    {
        if(true == m_module_data.sleep.sleep_list[i].in_use)
        {
            m_module_data.sleep.sleep_list[i].sleep_data.current_ms++;
            if(m_module_data.sleep.sleep_list[i].sleep_data.current_ms >= m_module_data.sleep.sleep_list[i].sleep_data.ms)
            {
                task_ready = true;
            }
        }
    }
    if(true == task_ready)
    {
        do
        {
            timeout = false;
            for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list); i++)
            {
                if(true == m_module_data.sleep.sleep_list[i].in_use)
                {
                    if(m_module_data.sleep.sleep_list[i].sleep_data.current_ms >= m_module_data.sleep.sleep_list[i].sleep_data.ms)
                    {
                        if(SIMPLY_THREAD_TASK_SUSPENDED == m_module_data.sleep.sleep_list[i].sleep_data.task_adjust->state)
                        {
                            PRINT_MSG("\tTask %s Ready From Timer\r\n",  m_module_data.sleep.sleep_list[i].sleep_data.task_adjust);
                            simply_thread_set_task_state_from_locked(m_module_data.sleep.sleep_list[i].sleep_data.task_adjust, SIMPLY_THREAD_TASK_READY);
                            assert(true == simply_thread_master_mutex_locked()); //We must be locked
                        }
                    }
                }
            }
        }
        while(true == timeout);
    }
}


/**
 * Function that resets the simply thread library.  Closes all existing created threads
 */
void simply_thread_reset(void)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    m_init_master_semaphore();
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
    assert(NULL != simply_thread_create_timer(m_maint_timer, "SYSTEM Timer", 1, SIMPLY_THREAD_TIMER_REPEAT, true));
    MUTEX_RELEASE();
}

/**
 * @brief cleanup the simply thread module;  Will kill all running tasks
 */
void simply_thread_cleanup(void)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    m_init_master_semaphore();
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
        simply_thread_sleep_ns(10);
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
 * @brief Function that sleeps for the specified number of milliseconds
 * @param ms The number of milliseconds to sleep
 */
void simply_thread_sleep_ms(unsigned long ms)
{
    ROOT_PRINT("%s\r\n", __FUNCTION__);
    struct simply_thread_task_s *ptr_task;
    bool index_found;
    unsigned int index = 500;
    MUTEX_GET();
    ptr_task = simply_thread_get_ex_task();
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
        index_found = false;
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list) && false == index_found; i++)
        {
            if(false == m_module_data.sleep.sleep_list[i].in_use)
            {
                index_found = true;
                m_module_data.sleep.sleep_list[i].sleep_data.current_ms = 0;
                m_module_data.sleep.sleep_list[i].sleep_data.ms = ms;
                m_module_data.sleep.sleep_list[i].sleep_data.task_adjust = ptr_task;
                m_module_data.sleep.sleep_list[i].in_use = true;
                index = i;
            }
        }
        assert(index < ARRAY_MAX_COUNT(m_module_data.sleep.sleep_list));
        simply_thread_set_task_state_from_locked(ptr_task, SIMPLY_THREAD_TASK_SUSPENDED);
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
    while(0 != simply_thread_sem_wait(&cond->sig_sem)){}
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
 * @brief Function that checks if a task is already waiting on the master mutex
 * @return NULL on error otherwise pointer to the repeat object
 */
static struct simply_thread_master_mutex_fifo_entry_s * simply_thread_thread_waiting_master_mutex(void)
{
    pthread_t current;
    uint8_t id;
    unsigned int repeat_count;
    struct simply_thread_master_mutex_fifo_entry_s * rv;
    current = pthread_self();
    id = m_get_thread_id();
    repeat_count = 0;
	rv = NULL;
	for(unsigned int i=0; i<ARRAY_MAX_COUNT(m_module_data.master_sem_data.fifo.entries); i++)
	{
		if(true == m_module_data.master_sem_data.fifo.entries[i].in_use)
		{
            printf("comparing %p to %p\r\n", current, m_module_data.master_sem_data.fifo.entries[i].thread);
            if(current == m_module_data.master_sem_data.fifo.entries[i].thread || id == m_module_data.master_sem_data.fifo.entries[i].id)
            {
            	//Sanity Check
            	assert(current == m_module_data.master_sem_data.fifo.entries[i].thread);
            	assert(id == m_module_data.master_sem_data.fifo.entries[i].id);

                repeat_count++;
                if(NULL == rv)
                {
                    rv = &m_module_data.master_sem_data.fifo.entries[i];
                }
            }
		}
	}
	//Sanity Checks
	assert(repeat_count<2);
	if(0 == repeat_count)
	{
		assert(NULL == rv);
	}
	return rv;
}

/**
 * @brief Function that fetches the next entry in the master fifo
 * @return NULL on error otherwise the first available entry
 */
static struct simply_thread_master_mutex_fifo_entry_s * simply_thread_master_fifo_nxt_entry(void)
{
	struct simply_thread_master_mutex_fifo_entry_s * rv = NULL;
	for(unsigned int i=0; i<ARRAY_MAX_COUNT(m_module_data.master_sem_data.fifo.entries) && NULL == rv; i++)
	{
		if(false == m_module_data.master_sem_data.fifo.entries[i].in_use)
		{
			rv = &m_module_data.master_sem_data.fifo.entries[i];
		}
	}
	return rv;
}

static void simply_thread_master_mutex_copy_entry(struct simply_thread_master_mutex_fifo_entry_s * one, struct simply_thread_master_mutex_fifo_entry_s * two)
{
	one->in_use = two->in_use;
	one->sem = two->sem;
	one->thread = two->thread;
}

/**
 * @brief Cleanup the master mutex fifo list
 */
static void simply_thread_master_mutex_fifo_cleanup(void)
{
	if(false == m_module_data.master_sem_data.fifo.entries[0].in_use)
	{
		for(unsigned int i=1; i<ARRAY_MAX_COUNT(m_module_data.master_sem_data.fifo.entries) && true == m_module_data.master_sem_data.fifo.entries[i].in_use; i++)
		{
			simply_thread_master_mutex_copy_entry(&m_module_data.master_sem_data.fifo.entries[i-1], &m_module_data.master_sem_data.fifo.entries[i]);
            m_module_data.master_sem_data.fifo.entries[i].in_use = false;
        }
	}
}

/**
 * @brief Function that prints the pop order
 */
static void print_pop_queue(void)
{
#ifdef DEBUG_MASTER_MUTEX
	MM_PRINT_MSG("Pop Queue\r\n");
	for(unsigned int i=0; i<ARRAY_MAX_COUNT(m_module_data.master_sem_data.fifo.entries); i++)
	{
		if(true == m_module_data.master_sem_data.fifo.entries[i].in_use)
        {
            MM_PRINT_MSG("\tPop Sem: %p %i\r\n", m_module_data.master_sem_data.fifo.entries[i].sem.sem, i);
        }
		else
		{
			return;
		}
	}
#endif //DEBUG_MASTER_MUTEX
}

/**
 * @brief Function that gets the master mutex
 * @return true on success
 */
bool simply_thread_get_master_mutex(void)
{
    bool sync_required = false;
    struct simply_thread_master_mutex_fifo_entry_s * new_ent;
    struct simply_thread_master_mutex_fifo_entry_s * repeat_ent;
    struct simply_thread_master_mutex_fifo_entry_s swap_1;
    struct simply_thread_master_mutex_fifo_entry_s swap_2;
    simply_thread_sem_t * block_sem;
    assert(NULL != m_module_data.master_semaphore.sem);
    while(0 != simply_thread_sem_wait(&m_module_data.master_semaphore)) {}
    MM_DEBUG_MESSAGE("Getting the master mutex\r\n");
    block_sem = NULL;
    m_module_data.master_sem_data.fifo.count++;
    if(1 < m_module_data.master_sem_data.fifo.count)
    {
        MM_PRINT_MSG("%s: Master Mutex Count %u\r\n", __FUNCTION__, m_module_data.master_sem_data.fifo.count);
        new_ent = simply_thread_master_fifo_nxt_entry();
        assert(NULL != new_ent);
        repeat_ent = simply_thread_thread_waiting_master_mutex();
        MM_DEBUG_MESSAGE("The Mutex is not available we need to wait for it\r\n");
        assert(false == new_ent->in_use);
        new_ent->in_use = true;
        new_ent->thread = pthread_self();
        new_ent->id = m_get_thread_id();
        new_ent->sem = malloc(sizeof(simply_thread_sem_t));
        assert(NULL != new_ent->sem);
        simply_thread_sem_init(new_ent->sem);
        block_sem = new_ent->sem;
        sync_required = true;
        assert(0 == simply_thread_sem_trywait(block_sem));
        if(NULL != repeat_ent)
        {
            simply_thread_log(COLOR_MAGENTA, "Swap Required\r\n");
            MM_PRINT_MSG("%s: Swapping %p and %p\r\n", __FUNCTION__,
            		new_ent->sem.sem,
					repeat_ent->sem.sem);
            print_pop_queue();
            simply_thread_master_mutex_copy_entry(&swap_1, new_ent);
            simply_thread_master_mutex_copy_entry(&swap_2, repeat_ent);
            simply_thread_master_mutex_copy_entry(new_ent, &swap_2);
            simply_thread_master_mutex_copy_entry(repeat_ent, &swap_1);
            MM_PRINT_MSG("%s: Swapped %p and %p\r\n", __FUNCTION__,
            		new_ent->sem.sem,
					repeat_ent->sem.sem);
            print_pop_queue();
        }
    }
    if(true == sync_required)
    {
    	assert(NULL != block_sem)
        MM_PRINT_MSG("%s: %u waiting on semaphore at: %p\r\n", __FUNCTION__, m_module_data.master_sem_data.fifo.entries[current_index].thread, sync_ent->sem.sem);
        print_pop_queue();
        assert(0 == simply_thread_sem_post(&m_module_data.master_semaphore));
        simply_thread_log(COLOR_MAGENTA, "%u %p waiting on sem at %p\r\n", m_get_thread_id(), block_sem, block_sem->sem);
        while(0 != simply_thread_sem_wait(block_sem)) {}
        assert(m_post_info.allocated = block_sem);
        assert(m_post_info.semaphore = block_sem->sem);
        simply_thread_log(COLOR_MAGENTA, "%u %p finished waiting on sem at %p\r\n", m_get_thread_id(), block_sem, block_sem->sem);
        MM_PRINT_MSG("%s: %u Finished waiting on semaphore at: %p\r\n", __FUNCTION__, m_module_data.master_sem_data.fifo.entries[current_index].thread,
        		sync_ent->sem.sem);
        assert(NULL != new_ent);
        simply_thread_sem_destroy(block_sem);
        free(block_sem);
        MM_PRINT_MSG("%s: %u semaphore at: %p Destroyed\r\n", __FUNCTION__, m_module_data.master_sem_data.fifo.entries[current_index].thread, sync_sem.sem);
        while(0 != simply_thread_sem_wait(&m_module_data.master_semaphore)) {}
    }
    assert(0 == simply_thread_sem_post(&m_module_data.master_semaphore));
    simply_thread_log(COLOR_MAGENTA, "****** %u Has mutex\r\n",  m_get_thread_id());
    return true;
}

/**
 * @brief Function that releases the master mutex
 */
void simply_thread_release_master_mutex(void)
{
    simply_thread_sem_t * sync_sem;
    assert(NULL != m_module_data.master_semaphore.sem);
    simply_thread_log(COLOR_MAGENTA, "****** %u releasing mutex\r\n",  m_get_thread_id());
    MM_DEBUG_MESSAGE("Starting pop\r\n");
    while(0 != simply_thread_sem_wait(&m_module_data.master_semaphore)) {}
    if(0 < m_module_data.master_sem_data.fifo.count)
    {
        MM_DEBUG_MESSAGE("Releasing master mutex\r\n");
        print_pop_queue();
        MM_DEBUG_MESSAGE("Post Next Sem\r\n");
        if(true == m_module_data.master_sem_data.fifo.entries[0].in_use)
        {
            assert(1 < m_module_data.master_sem_data.fifo.count); //Sanity Check
            sync_sem = m_module_data.master_sem_data.fifo.entries[0].sem;
            m_module_data.master_sem_data.fifo.count--;
            MM_PRINT_MSG("%s: Master Mutex Count %u\r\n", __FUNCTION__, m_module_data.master_sem_data.fifo.count);
            MM_PRINT_MSG("%s: posting to sem at %p\r\n", __FUNCTION__, sync_sem->sem);
            simply_thread_log(COLOR_MAGENTA, "posting to sem at %p\r\n", sync_sem->sem);
            m_post_info.semaphore = sync_sem->sem;
            m_post_info.allocated = sync_sem;
            assert(0 == simply_thread_sem_post(sync_sem));
            m_module_data.master_sem_data.fifo.entries[0].in_use = false;
			simply_thread_master_mutex_fifo_cleanup();
			print_pop_queue();
            simply_thread_log(COLOR_MAGENTA, "posted to sem at %p\r\n", sync_sem->sem);
        }
        else
        {
            MM_PRINT_MSG("%s: Nothing to do at index: %i\r\n", __FUNCTION__, current_index);
            m_module_data.master_sem_data.fifo.count--;
            MM_PRINT_MSG("%s: Master Mutex Count %u\r\n", __FUNCTION__, m_module_data.master_sem_data.fifo.count);
        }
    }
    simply_thread_log(COLOR_MAGENTA, "****** %u released mutex\r\n",  m_get_thread_id());
    assert(0 == simply_thread_sem_post(&m_module_data.master_semaphore));
}

/**
 * @brief Function that checks if the master mutex is locked
 * @return true if the master mutex is locked
 */
bool simply_thread_master_mutex_locked(void)
{
    if(0 < m_module_data.master_sem_data.fifo.count)
    {
        MM_PRINT_MSG("Master Mutex is Locked\r\n");
        return true;
    }
    MM_PRINT_MSG("Master Mutex NOT Locked\r\n");
    return false;
}

/**
 * @brief Function that initializes the master semaphore
 */
static void m_init_master_semaphore(void)
{

    if(NULL == m_module_data.master_semaphore.sem)
    {
        assert(0 == pthread_mutex_lock(&m_module_data.init_mutex));
        if(NULL == m_module_data.master_semaphore.sem)
        {
            simply_thread_sem_init(&m_module_data.master_semaphore);
            for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_module_data.master_sem_data.fifo.entries); i++)
            {
                m_module_data.master_sem_data.fifo.entries[i].in_use = false;
            }
            m_module_data.master_sem_data.fifo.count = 0;
        }
        pthread_mutex_unlock(&m_module_data.init_mutex);
        MM_PRINT_MSG("Initialized the master mutex\r\n");
    }
}

static inline void _print_task_stats(struct simply_thread_task_s * tcb)
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
    ST_LOG_ERROR("Master Mutex Count %u\r\n", m_module_data.master_sem_data.fifo.count);
    MUTEX_RELEASE();
}
