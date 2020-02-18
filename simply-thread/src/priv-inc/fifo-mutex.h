#ifndef FIFO_MUTEX_H_
#define FIFO_MUTEX_H_

#include <stdbool.h>

typedef void * fifo_mutex_entry_t; //!< typedef for pulling and pushing an entry of of and onto the fifo queue

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

/**
 * Pull the fifo entry off of the fifo queue for the current task
 * @return NULL if entry does not exist.
 */
fifo_mutex_entry_t fifo_mutex_pull(void);

/**
 * @brief push a previously pulled entry back onto the fifo
 * @param entry
 */
void fifo_mutex_push(fifo_mutex_entry_t entry);

/**
 * Function that makes the fifo mutex safe to be interupted
 */
void fifo_mutex_prep_signal(void);

/**
 * Function that tells the fifo mutex that the signal has been sent
 */
void fifo_mutex_clear_signal(void);

#endif //#ifndef FIFO_MUTEX_H_
