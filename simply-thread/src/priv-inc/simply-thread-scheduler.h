/**
 * @file simply-thread-scheduler.h
 * @author Kade Cox
 * @date Created: Dec 17, 2019
 * @details
 *
 */

#ifndef SIMPLY_THREAD_SCHEDULER_H_
#define SIMPLY_THREAD_SCHEDULER_H_

#include <simply-thread.h>
#include <simply-thread-objects.h>
#include <simply-thread-linked-list.h>

/**
 * @brief initialize the module
 */
void simply_thread_scheduler_init(void);

/**
 * @brief Kill the scheduler and cleanup the module
 */
void simply_thread_scheduler_kill(void);

/**
 * @brief tell the scheduler to run
 * @param thread_data Data for the scheduler to use
 */
void simply_thread_run(struct simply_thread_scheduler_data_s *thread_data);

/**
 * @brief tell the scheduler that a task has gone to sleep
 * @param ptr_task
 */
void simply_thread_tell_sched_task_sleeping(struct simply_thread_task_s *ptr_task);

/**
 * @brief tell the scheduler that a task has been scheduled
 */
void simply_thread_tell_sched_task_signaled(void);
#endif /* SIMPLY_THREAD_SCHEDULER_H_ */
