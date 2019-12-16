/**
 * @file priv-simply-thread.h
 * @author Kade Cox
 * @date Created: Dec 16, 2019
 * @details
 * Private APIS for simply thread
 */

#include <simply-thread-objects.h>

#ifndef PRIV_SIMPLY_THREAD_H_
#define PRIV_SIMPLY_THREAD_H_

/**
 * @brief Update a tasks state
 * @param task
 * @param state
 */
void simply_thread_set_task_state(struct simply_thread_task_s *task, enum simply_thread_thread_state_e state);

#endif /* PRIV_SIMPLY_THREAD_H_ */
