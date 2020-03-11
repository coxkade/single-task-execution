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
#include <simply-thread-timers.h>
#include <simply-thread-mutex.h>
#include <simply_thread_system_clock.h>
#include <simply-thread-sem-helper.h>
#include <TCB.h>
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
    simply_thread_sem_t sem;
    tcb_task_t * task;
}; //!< Structure to help with the sleep on tick handler

struct simply_thread_main_module_data_s
{
	bool cleaning_up;
}; //Structure for the main module data

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

static struct simply_thread_main_module_data_s simply_thread_module_data = {
		.cleaning_up = false
};

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

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
        if(true == simply_thread_module_data.cleaning_up)
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
 * Function that resets the simply thread library.  Closes all existing created threads
 */
void simply_thread_reset(void)
{
	simply_thread_cleanup();
}

/**
 * @brief cleanup the simply thread module;  Will kill all running tasks
 */
void simply_thread_cleanup(void)
{
	simply_thread_module_data.cleaning_up = true;
	tcb_reset(); //Destroy all running tcb tasks
	simply_thead_system_clock_reset(); //Reset the system clock
	simply_thread_module_data.cleaning_up = false;
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
	simply_thread_task_t rv;
	if(NULL == name || NULL == cb || (data != NULL && 0 < data_size))
	{
		return NULL;
	}
	rv = tcb_create_task(name, cb, priority, data, data_size);
	return rv;
}

/**
 * @brief the Sleep Tick Handler
 * @param handle
 * @param tickval
 * @param args
 */
void simply_thread_sleep_tick_handler(sys_clock_on_tick_handle_t handle, uint64_t tickval, void *args)
{
	struct sleep_tick_data_s * typed;
	typed = args;
	SS_ASSERT(NULL != typed);
	if(NULL != typed->task)
	{
		//Interrupt context is sleeping
		if(SIMPLY_THREAD_TASK_SUSPENDED != tcb_get_task_state(typed->task))
		{
			return;
		}
	}
	if(typed->count < typed->max_count)
	{
		typed->count++;
		if(typed->count == typed->max_count)
		{
			//We have finished sleeping and need to let the sleeping context know
			if(NULL == typed->task)
			{
				SS_ASSERT(0 == simply_thread_sem_post(&typed->sem));
			}
			else
			{
				tcb_set_task_state(SIMPLY_THREAD_TASK_READY, typed->task);
			}
		}
	}
}

/**
 * @brief Function that sleeps for the specified number of milliseconds
 * @param ms The number of milliseconds to sleep
 */
void simply_thread_sleep_ms(unsigned long ms)
{
	sys_clock_on_tick_handle_t tick_handle;
	struct sleep_tick_data_s sleep_data;
	sleep_data.task = tcb_task_self();
	sleep_data.count = 0;
	sleep_data.max_count = ms;
	if(NULL == sleep_data.task)
	{
		//We are in the interrupt context
		simply_thread_sem_init(&sleep_data.sem);
		SS_ASSERT(EAGAIN == simply_thread_sem_wait(&sleep_data.sem));
		tick_handle = simply_thead_system_clock_register_on_tick(simply_thread_sleep_tick_handler, &sleep_data);
		SS_ASSERT(NULL != tick_handle);
		while(0 != simply_thread_sem_wait(&sleep_data.sem)) {}
		//finished waiting destroy the semaphore
		simply_thread_sem_destroy(&sleep_data.sem);
	}
	else
	{
		//We are not in the interrupt context
		tick_handle = simply_thead_system_clock_register_on_tick(simply_thread_sleep_tick_handler, &sleep_data);
		SS_ASSERT(NULL != tick_handle);
		tcb_set_task_state(SIMPLY_THREAD_TASK_SUSPENDED, sleep_data.task);
	}
	SS_ASSERT(NULL != tick_handle);
	simply_thead_system_clock_deregister_on_tick(tick_handle);
}

/**
 * @brief Function that suspends a task
 * @param handle
 * @return true on success
 */
bool simply_thread_task_suspend(simply_thread_task_t handle)
{
	if(NULL == handle)
	{
		return false;
	}
	tcb_set_task_state(SIMPLY_THREAD_TASK_SUSPENDED, handle);
	return true;
}

/**
 * @brief Function that resumes a task
 * @param handle
 * @return true on success
 */
bool simply_thread_task_resume(simply_thread_task_t handle)
{
	if(NULL == handle)
	{
		return false;
	}
	tcb_set_task_state(SIMPLY_THREAD_TASK_READY, handle);
	return true;
}

/**
 * @brief Function that gets a tasks state
 * @param handle Handle of the task to get the state
 * @return The state of the task
 */
enum simply_thread_thread_state_e simply_thread_task_state(simply_thread_task_t handle)
{
	SS_ASSERT(NULL != handle);
	return tcb_get_task_state(handle);
}


/**
 * @brief Function that checks if we are currently in an interrupt
 * @return true currently in the interrupt context.
 * @return false  Not Currently in the interrupt context.
 */
bool simply_thread_in_interrupt(void)
{
	if(NULL == tcb_task_self())
	{
		return true;
	}
	return false;
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
        bool run_now);

/**
 * @brief Function that starts a simply thread timer
 * @param timer the handle of the timer to start
 * @return true on success
 */
bool simply_thread_timer_start(simply_thread_timer_t timer);

/**
 * @brief Function that stops a simply thread timer
 * @param timer the handle of the timer to stop
 * @return true on success
 */
bool simply_thread_timer_stop(simply_thread_timer_t timer);

/**
 * @brief Function that creates a mutex
 * @param name The name of the mutex
 * @return NULL on error.  Otherwise the mutex handle
 */
simply_thread_mutex_t simply_thread_mutex_create(const char *name);

/**
 * @brief Function that unlocks a mutex
 * @param mux the mutex handle in question
 * @return true on success
 */
bool simply_thread_mutex_unlock(simply_thread_mutex_t mux);

/**
 * @brief Function that locks a mutex
 * @param mux handle of the mutex to lock
 * @param wait_time How long to wait to obtain a lock
 * @return true on success
 */
bool simply_thread_mutex_lock(simply_thread_mutex_t mux, unsigned int wait_time);

/**
 * @brief Function that creates a queue
 * @param name String containing the name of the queue
 * @param queue_size The number of elements allowed in the queue
 * @param element_size The size of each element in the queue
 * @return NULL on error.  Otherwise the created Queue Handle
 */
simply_thread_queue_t simply_thread_queue_create(const char *name, unsigned int queue_size, unsigned int element_size);

/**
 * @brief Function that gets the current number of elements on the queue
 * @param queue handle of the queue in question
 * @return 0xFFFFFFFF on error. Otherwise the queue count
 */
unsigned int simply_thread_queue_get_count(simply_thread_queue_t queue);

/**
 * @brief Function that places data on the queue
 * @param queue handle of the queue in question
 * @param data ptr to the data to place on the queue
 * @param block_time how long to wait on the queue
 * @return true on success, false otherwise
 */
bool simply_thread_queue_send(simply_thread_queue_t queue, void *data, unsigned int block_time);


/**
 * @brief Function that retrieves data from the queue
 * @param queue handle of the queue in question
 * @param data ptr to the object to place the data in
 * @param block_time how long to wait on the queue
 * @return true on success, false otherwise
 */
bool simply_thread_queue_rcv(simply_thread_queue_t queue, void *data, unsigned int block_time);

/**
 * @brief Function that prints the contents of the tcb
 */
void simply_thread_print_tcb(void);


