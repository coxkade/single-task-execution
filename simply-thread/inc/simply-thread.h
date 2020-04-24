/**
 * @file simply-thread.h
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#ifndef SIMPLY_THREAD_H_
#define SIMPLY_THREAD_H_

//#define SS_ASSERT(...) simply_thread_assert((__VA_ARGS__), __FILE__, __LINE__, #__VA_ARGS__)
#define SS_ASSERT(...) assert(__VA_ARGS__)

#define SIMPLY_THREAD_PRINT(...) simply_thread_log(COLOR_RESET, __VA_ARGS__)

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

enum simply_thread_thread_state_e
{
    SIMPLY_THREAD_TASK_RUNNING = 0,
    SIMPLY_THREAD_TASK_READY,
    SIMPLY_THREAD_TASK_BLOCKED,
    SIMPLY_THREAD_TASK_SUSPENDED,
    SIMPLY_THREAD_TASK_UNKNOWN_STATE,
    SIMPLY_THREAD__TASK_STATE_COUNT
}; //!< Enum for the different task states


/**
 * Function that resets the simply thread library.  Closes all existing created threads
 */
void simply_thread_reset(void);

/**
 * @brief cleanup the simply thread module;  Will kill all running tasks
 */
void simply_thread_cleanup(void);

/**
 * Fetch the current task handle
 */
void *simply_thread_current_task_handle(void);

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
 * @brief Function that gets a tasks state
 * @param handle Handle of the task to get the state
 * @return The state of the task
 */
enum simply_thread_thread_state_e simply_thread_task_state(simply_thread_task_t handle);

/**
 * @brief Function that checks if we are currently in an interrupt
 * @return true currently in the interrupt context.
 * @return false  Not Currently in the interrupt context.
 */
bool simply_thread_in_interrupt(void);

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
 * Function that handles our asserts
 * @param result
 * @param file
 * @param line
 * @param expression
 */
void simply_thread_assert(bool result, const char *file, unsigned int line, const char *expression);




#endif /* SIMPLY_THREAD_H_ */
