/**
 * @file simply-thread-timers.h
 * @author Kade Cox
 * @date Created: Jan 20, 2020
 * @details
 * Function for working with simply thread timers
 */

#include <simply-thread.h>

#ifndef SIMPLY_THREAD_TIMERS_H_
#define SIMPLY_THREAD_TIMERS_H_

/**
 * @brief Set up the simply thread timers
 */
void simply_thread_timers_init(void);

/**
 * @brief Destroy all simply thread timers
 */
void simply_thread_timers_destroy(void);

#endif /* SIMPLY_THREAD_TIMERS_H_ */
