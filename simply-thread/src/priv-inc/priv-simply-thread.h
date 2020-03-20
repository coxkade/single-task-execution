/**
 * @file priv-simply-thread.h
 * @author Kade Cox
 * @date Created: Dec 16, 2019
 * @details
 * Private APIS for simply thread
 */

#include <simply-thread-objects.h>
#include <simply-thread-log.h>
#include <TCB.h>
#include <stdlib.h>

#ifndef PRIV_SIMPLY_THREAD_H_
#define PRIV_SIMPLY_THREAD_H_

#ifndef ST_NS_PER_MS
#define ST_NS_PER_MS 1000000
#endif //ST_NS_PER_MS


///**
// * @brief execute the scheduler from a locked context
// */
//void simply_ex_sched_from_locked(void);

///**
// * @brief Update a tasks state
// * @param task
// * @param state
// */
//void simply_thread_set_task_state(struct simply_thread_task_s *task, enum simply_thread_thread_state_e state);
//
///**
// * @brief Update a tasks state
// * @param task
// * @param state
// */
//void simply_thread_set_task_state_from_locked(struct simply_thread_task_s *task, enum simply_thread_thread_state_e state);

/**
 * @brief Function that sleeps for the specified number of nanoseconds
 * @param ns number of nanoseconds to sleep
 */
void simply_thread_sleep_ns(unsigned long ns);

///**
// * @brief Function that fetches the simply thread library data
// * @return pointer to the library data
// */
//struct simply_thread_lib_data_s *simply_thread_lib_data(void);

///**
// * @brief get a pointer to the task calling this function
// * @return NULL on error otherwise a task pointer
// */
//struct simply_thread_task_s *simply_thread_get_ex_task(void);

///**
// * @brief Function that gets the master mutex
// * @return true on success
// */
//bool simply_thread_get_master_mutex(void);
//
///**
// * @brief Function that releases the master mutex
// */
//void simply_thread_release_master_mutex(void);

///**
// * @brief Function that checks if the master mutex is locked
// * @return true if the master mutex is locked
// */
//bool simply_thread_master_mutex_locked(void);

///**
// * @brief Internal implementation of snprintf
// * @param s The too buffer
// * @param n the max size of the buffer
// * @param format The format of the string
// * @return The size of the string
// */
//int simply_thread_snprintf(char *s, size_t n, const char *format, ...);

#endif /* PRIV_SIMPLY_THREAD_H_ */
