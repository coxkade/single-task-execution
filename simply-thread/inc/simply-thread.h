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
    SIMPLY_THREAD_TIMER_ONE_SHOT,
    SIMPLY_THREAD_TIMER_REPEAT
} simply_thread_timer_type_e; //!< Enum that details the timer type


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

#endif /* SIMPLY_THREAD_H_ */
