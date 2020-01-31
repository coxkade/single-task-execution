/**
 * @file priv-simply-thread.h
 * @author Kade Cox
 * @date Created: Dec 16, 2019
 * @details
 * Private APIS for simply thread
 */

#include <simply-thread-objects.h>
#include <simply-thread-queue.h>

#ifndef PRIV_SIMPLY_THREAD_H_
#define PRIV_SIMPLY_THREAD_H_

/**
 * @brief execute the scheduler from a locked context
 */
void simply_ex_sched_from_locked(void);

/**
 * @brief Update a tasks state
 * @param task
 * @param state
 */
void simply_thread_set_task_state(struct simply_thread_task_s *task, enum simply_thread_thread_state_e state);

/**
 * @brief Update a tasks state
 * @param task
 * @param state
 */
void simply_thread_set_task_state_from_locked(struct simply_thread_task_s *task, enum simply_thread_thread_state_e state);

/**
 * @brief Function that sleeps for the specified number of nanoseconds
 * @param ns number of nanoseconds to sleep
 */
void simply_thread_sleep_ns(unsigned long ns);

/**
 * @brief initialize a condition
 * @param cond
 */
void simply_thread_init_condition(struct simply_thread_condition_s *cond);

/**
 * @brief Destroy a condition
 * @param cond
 */
void simply_thread_dest_condition(struct simply_thread_condition_s *cond);

/**
 * @brief send a condition
 * @param cond
 */
void simply_thread_send_condition(struct simply_thread_condition_s *cond);

/**
 * @brief wait on a condition
 * @param cond
 */
void simply_thread_wait_condition(struct simply_thread_condition_s *cond);

/**
 * @brief get a pointer to the task calling this function
 * @return NULL on error otherwise a task pointer
 */
struct simply_thread_task_s *simply_thread_get_ex_task(void);

/**
 * @brief Function that fetches the simply thread library data
 * @return pointer to the library data
 */
struct simply_thread_lib_data_s *simply_thread_lib_data(void);

#endif /* PRIV_SIMPLY_THREAD_H_ */
