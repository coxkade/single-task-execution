/**
 * @file simply-thread-sem-helper.h
 * @author Kade Cox
 * @date Created: Jan 31, 2020
 * @details
 * Helper for dealing with semaphores
 */

#include <semaphore.h>
#include <errno.h>

#ifndef SIMPLY_THREAD_SEM_HELPER_H_
#define SIMPLY_THREAD_SEM_HELPER_H_

typedef struct simply_thread_sem_t
{
    void *data;  //!< Additional data for the semaphore
    int id; //!< The semaphore id
} simply_thread_sem_t; //!< Structure for holding my semaphore

/**
 * @brief Initialize a semaphore
 * @param sem
 */
void simply_thread_sem_init(simply_thread_sem_t *sem);

/**
 * Destroy a semaphore
 * @param sem
 */
void simply_thread_sem_destroy(simply_thread_sem_t *sem);

/**
 * Blocking Wait for a semaphor
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_wait(simply_thread_sem_t *sem);

/**
 * Nonblocking semaphore wait
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_trywait(simply_thread_sem_t *sem);

/**
 * @brief blocking semaphore wait with a timeout
 * @param sem
 * @param ms The max number of ms to wait for
 * @return o on success
 */
int simply_thread_sem_timed_wait(simply_thread_sem_t *sem, unsigned int ms);

/**
 * Semaphore post
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_post(simply_thread_sem_t *sem);

/**
 * @brief Function that unlinks created semaphores so they can be freed when tests complete
 */
void sem_helper_cleanup(void);

/**
 * Cleanup function to call on exit
 */
void sem_helper_clear_master(void);

#endif /* SIMPLY_THREAD_SEM_HELPER_H_ */
