#ifndef FIFO_MUTEX_H_
#define FIFO_MUTEX_H_

#include <stdbool.h>

/**
 * @brief fetch the mutex
 * @return true on success
 */
bool fifo_mutex_get(void);

/**
 * @brief release the fifo mutex
 */
void fifo_mutex_release(void);

/**
 * @brief Reset the fifo mutex module
 */
void fifo_mutex_reset(void);

/**
 * @brief tells if the fifo mutex is locked
 * @return true if the mutex is currently locked
 */
bool fifo_mutex_locked(void);

#endif //#ifndef FIFO_MUTEX_H_
