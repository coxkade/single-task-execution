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

typedef void *simply_thread_task_t; //!< Typedef for a handle for a task created with simply thread

typedef void (*simply_thread_task_fnct)(void *data, uint16_t data_size);  //!< Typedef for a function used with simply thread

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

#endif /* SIMPLY_THREAD_H_ */
