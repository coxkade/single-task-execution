/**
 * @file simply_thread_system_clock.h
 * @author Kade Cox
 * @date Created: Feb 28, 2020
 * @details
 * Module for the system clock for the simply thread library
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef SIMPLY_THREAD_SRC_PRIV_INC_SIMPLY_THREAD_SYSTEM_CLOCK_H_
#define SIMPLY_THREAD_SRC_PRIV_INC_SIMPLY_THREAD_SYSTEM_CLOCK_H_

//Macro for the simply thread system clock tick count
#define SIMPLY_THREAD_MAX_SYSTEM_CLOCK_TICKS (0xFFFFFFFFFFFFFFFF)

typedef void *sys_clock_on_tick_handle_t;  //!< Typedef for the ontick handle

/**
 * @brief Function that resets the simply thread clock logic
 */
void simply_thead_system_clock_reset(void);

/**
 * @brief Function that registers a function to be called on a tick
 * @param on_tick pointer to the function to call on tick
 * @param args argument to pass to the on tick handler when it is called
 * @return NULL on error.  Otherwise the registered id
 */
sys_clock_on_tick_handle_t simply_thead_system_clock_register_on_tick(void (*on_tick)(sys_clock_on_tick_handle_t handle, uint64_t tickval,
        void *args), void *args);

/**
 * @brief Function the deregisters a function on tick
 * @param handle the handle to deregister
 */
void simply_thead_system_clock_deregister_on_tick(sys_clock_on_tick_handle_t handle);

/**
 * Function that tells if it is safe to interrupt a task.  Must be called from a locked context
 * @return true if safe.
 */
bool simply_thead_system_clock_safe_to_interrupt(void);

#endif /* SIMPLY_THREAD_SRC_PRIV_INC_SIMPLY_THREAD_SYSTEM_CLOCK_H_ */
