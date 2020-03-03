#ifndef FIFO_MUTEX_H_
#define FIFO_MUTEX_H_

#include <stdbool.h>

typedef void *master_mutex_entry_t;  //!< typedef for pulling and pushing an entry of of and onto the fifo queue

/**
 * @brief fetch the mutex
 * @return true on success
 */
bool master_mutex_get(void);

/**
 * @brief release the master mutex
 */
void master_mutex_release(void);

/**
 * @brief Reset the master mutex module
 */
void master_mutex_reset(void);

/**
 * @brief tells if the master mutex is locked
 * @return true if the mutex is currently locked
 */
bool master_mutex_locked(void);

/**
 * Try and lock the master mutex
 * @return true if locked
 */
bool master_mutex_trylock(void);

/**
 * Pull the fifo entry off of the fifo queue for the current task
 * @return NULL if entry does not exist.
 */
master_mutex_entry_t master_mutex_pull(void);

/**
 * @brief push a previously pulled entry back onto the fifo
 * @param entry
 */
void master_mutex_push(master_mutex_entry_t entry);

/**
 * Function that makes the fifo mutex safe to be interupted
 */
void master_mutex_prep_signal(void);

/**
 * Function That clears the prep flags
 */
void master_mutex_clear_prep_signal(void);

#endif //#ifndef FIFO_MUTEX_H_
