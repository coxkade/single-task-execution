/**
 * @file simply-thread-mutex.h
 * @author Kade Cox
 * @date Created: Jan 20, 2020
 * @details
 * module that handles mutexes for the simply thread library.
 */

#include <simply-thread.h>
#include <simply-thread-objects.h>
#include <simply-thread-linked-list.h>

#ifndef SIMPLY_THREAD_MUTEX_H_
#define SIMPLY_THREAD_MUTEX_H_

/**
 * @brief Function that initializes the simply thread mutex module
 */
void simply_thread_mutex_init(void);

/**
 * @brief Function that cleans up the simply thread mutexes
 */
void simply_thread_mutex_cleanup(void);

/**
 * @brief function the systic needs to call
 */
void simply_thread_mutex_maint(void);

#endif /* SIMPLY_THREAD_MUTEX_H_ */
