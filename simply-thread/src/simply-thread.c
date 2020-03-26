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
#include <Sem-Helper.h>
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
#include <execinfo.h>

//#define DEBUG_SIMPLY_THREAD

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_YELLOW, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

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
    sem_helper_sem_t sem;
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

//	printf("reseting tcb\r\n");
//	tcb_reset(); //Destroy all running tcb tasks
	PRINT_MSG("reseting tcb\r\n");
	tcb_reset(); //Destroy all running tcb tasks
	PRINT_MSG("resetting the system clock\r\n");
	simply_thead_system_clock_reset(); //Reset the system clock
	simply_thread_timers_cleanup();
	simply_thread_mutex_cleanup();
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
	if(NULL == name || NULL == cb || (data == NULL && 0 != data_size))
	{
		ST_LOG_ERROR("Name: %p, cb: %p, data: %p, data size: %u\r\n", name, cb, data, data_size);
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
	enum simply_thread_thread_state_e cstate;
	typed = args;
	SS_ASSERT(NULL != typed);

	if(NULL != typed->task)
	{
		//Interrupt context is sleeping
		cstate = tcb_get_task_state(typed->task);
		PRINT_MSG("tcb_get_task_state retunred %i\r\n", cstate);
		if(SIMPLY_THREAD_TASK_SUSPENDED != cstate)
		{
			PRINT_MSG("%s Waiting on SIMPLY_THREAD_TASK_SUSPENDED \r\n", __FUNCTION__);
			return;
		}
	}
	if(typed->count < typed->max_count)
	{
		typed->count++;
		if(typed->count == typed->max_count)
		{
			PRINT_MSG("%s Reached Count: %u \r\n", __FUNCTION__, typed->count);
			//We have finished sleeping and need to let the sleeping context know
			if(NULL == typed->task)
			{
				PRINT_MSG("%s Posting to the interrupt semaphore\r\n", __FUNCTION__);
				SS_ASSERT(0 == Sem_Helper_sem_post(&typed->sem));
			}
			else
			{
				PRINT_MSG("%s Setting task %s to SIMPLY_THREAD_TASK_READY\r\n", __FUNCTION__, typed->task->name);
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
	PRINT_MSG("%s Starting %p\r\n", __FUNCTION__, pthread_self());
	sys_clock_on_tick_handle_t tick_handle;
	struct sleep_tick_data_s sleep_data;
	int result;
	sleep_data.task = tcb_task_self();
	sleep_data.count = 0;
	sleep_data.max_count = ms;
	if(NULL == sleep_data.task)
	{
		PRINT_MSG("\t%s Handling unknown task %p\r\n", __FUNCTION__, pthread_self());
		//We are in the interrupt context
		Sem_Helper_sem_init(&sleep_data.sem);
		result = Sem_Helper_sem_trywait(&sleep_data.sem);
		if(EAGAIN != result)
		{
			SS_ASSERT(EAGAIN == result);
		}
		tick_handle = simply_thead_system_clock_register_on_tick(simply_thread_sleep_tick_handler, &sleep_data);
		SS_ASSERT(NULL != tick_handle);
		PRINT_MSG("\t%s Waiting on semaphore %p\r\n", __FUNCTION__, pthread_self());
		while(0 != Sem_Helper_sem_wait(&sleep_data.sem)) {}
		PRINT_MSG("\t%s Finished waiting on semaphore %p\r\n", __FUNCTION__, pthread_self());
		//finished waiting destroy the semaphore
		Sem_Helper_sem_destroy(&sleep_data.sem);
	}
	else
	{
		PRINT_MSG("\t%s Handling known task %p\r\n", __FUNCTION__, pthread_self());
		//We are not in the interrupt context
		tick_handle = simply_thead_system_clock_register_on_tick(simply_thread_sleep_tick_handler, &sleep_data);
		SS_ASSERT(NULL != tick_handle);
		tcb_set_task_state(SIMPLY_THREAD_TASK_SUSPENDED, sleep_data.task);
	}
	SS_ASSERT(NULL != tick_handle);
	simply_thead_system_clock_deregister_on_tick(tick_handle);
	PRINT_MSG("%s Finishing %p\r\n", __FUNCTION__, pthread_self());
}

/**
 * @brief Function that suspends a task
 * @param handle
 * @return true on success
 */
bool simply_thread_task_suspend(simply_thread_task_t handle)
{
	tcb_task_t * typed;
	typed = handle;
	if(NULL == typed)
	{
		typed = tcb_task_self();
		if(NULL == typed)
		{
			return false;
		}
	}
	tcb_set_task_state(SIMPLY_THREAD_TASK_SUSPENDED, typed);
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


/**
 * Function that handles our asserts
 * @param result
 * @param file
 * @param line
 * @param expression
 */
void simply_thread_assert(bool result, const char * file, unsigned int line, const char * expression)
{
	if(true != result)
	{
		ST_LOG_ERROR("%s Assert Failed Cleaning up\r\n\tLine: %i\r\n\tFile: %s\r\n", expression,  line, file);
        void* callstack[128];
        int i, frames = backtrace(callstack, 128);
        char** strs = backtrace_symbols(callstack, frames);
        for (i = 0; i < frames; ++i) {
            printf("%s\n", strs[i]);
        }
        free(strs);
		tcb_on_assert();
		printf("Exiting on assert\r\n");
		exit(-1);
	}
}

