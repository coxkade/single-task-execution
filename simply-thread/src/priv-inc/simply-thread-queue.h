/**
 * @file simply-thread-queue.h
 * @author Kade Cox
 * @date Created: Jan 23, 2020
 * @details
 *  Private Queue Functions
 */

#include <simply-thread.h>
#include <simply-thread-objects.h>
#include <simply-thread-linked-list.h>

#ifndef SIMPLY_THREAD_QUEUE_H_
#define SIMPLY_THREAD_QUEUE_H_

/**
 * @brief Function that initializes the simply thread queue module
 */
void simply_thread_queue_init(void);

/**
 * @brief Function that cleans up the simply thread queue
 */
void simply_thread_queue_cleanup(void);

/**
 * @brief function the systic needs to call
 */
void simply_thread_queue_maint(void);

#endif /* SIMPLY_THREAD_QUEUE_H_ */