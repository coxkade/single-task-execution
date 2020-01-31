/**
 * @file simply-thread-sem-helper.h
 * @author Kade Cox
 * @date Created: Jan 31, 2020
 * @details
 * Helper for dealing with semaphores
 */

#include <semaphore.h>

#ifndef SIMPLY_THREAD_SEM_HELPER_H_
#define SIMPLY_THREAD_SEM_HELPER_H_

typedef struct simply_thread_sem_t
{
    int count;
    sem_t *sem;
} simply_thread_sem_t; //Structure for holding my semaphore

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
 * Get a semaphores count
 * @param sem
 * @return the current semaphore count
 */
int simply_thread_sem_get_count(simply_thread_sem_t *sem);

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
 * Semaphore post
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_post(simply_thread_sem_t *sem);

#endif /* SIMPLY_THREAD_SEM_HELPER_H_ */
