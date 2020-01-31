/**
 * @file simply-thread.h
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 *
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef SIMPLY_THREAD_H_
#define SIMPLY_THREAD_H_

//Typedefs for tasks
typedef void *simply_thread_task_t; //!< Typedef for a handle for a task created with simply thread

typedef void (*simply_thread_task_fnct)(void *data, uint16_t data_size);  //!< Typedef for a function used with simply thread

//Typedefs for timers
typedef void *simply_thread_timer_t;  //!< Typedef for a handle for a timer created with simply thread

typedef void (*simply_thread_timer_cb)(simply_thread_timer_t timer_handle); //!< Typedef for the timer callback functions

typedef enum
{
    SIMPLY_THREAD_TIMER_ONE_SHOT,//!< SIMPLY_THREAD_TIMER_ONE_SHOT
    SIMPLY_THREAD_TIMER_REPEAT   //!< SIMPLY_THREAD_TIMER_REPEAT
} simply_thread_timer_type_e; //!< Enum that details the timer type

//Typedefs for mutexes
typedef void *simply_thread_mutex_t;   //!< Typedef for mutex handle

//Typedets for queues
typedef void *simply_thread_queue_t;  //!< Typedef for Queue Handle


/**
 * Function that resets the simply thread library.  Closes all existing created threads
 */
void simply_thread_reset(void);

/**
 * @brief cleanup the simply thread module;  Will kill all running tasks
 */
void simply_thread_cleanup(void);

/**
 * @brief Function that creates a new thread
 * @param name The name of the thread
 * @param cb the worker function of the thread
 * @param priority the priority of the thread
 * @param data the data to pass to the thread
 * @param data_size the size of the data to pass to the thread
 * @return handle of the new thread
 */
simply_thread_task_t simply_thread_new_thread(const char *name, simply_thread_task_fnct cb, unsigned int priority, void *data, uint16_t data_size);


/**
 * @brief Function that sleeps for the specified number of milliseconds
 * @param ms The number of milliseconds to sleep
 */
void simply_thread_sleep_ms(unsigned long ms);

/**
 * @brief Function that suspends a task
 * @param handle
 * @return true on success
 */
bool simply_thread_task_suspend(simply_thread_task_t handle);

/**
 * @brief Function that resumes a task
 * @param handle
 * @return true on success
 */
bool simply_thread_task_resume(simply_thread_task_t handle);

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


#endif /* SIMPLY_THREAD_H_ */
