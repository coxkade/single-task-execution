/**
 * @file Sem-Helper.h
 * @author Kade Cox
 * @date Created: Mar 19, 2020
 * @details
 *
 */

#ifndef SIMPLY_THREAD_SRC_TASK_HELPER_SEM_HELPER_H_
#define SIMPLY_THREAD_SRC_TASK_HELPER_SEM_HELPER_H_

typedef struct sem_helper_sem_t
{
    int id; //!< The semaphore id
} sem_helper_sem_t; //!< Structure for holding my semaphore

/**
 * @brief Initialize a semaphore
 * @param sem
 */
void Sem_Helper_sem_init(sem_helper_sem_t *sem);

/**
 * Destroy a semaphore
 * @param sem
 */
void Sem_Helper_sem_destroy(sem_helper_sem_t *sem);

/**
 * Blocking Wait for a semaphor
 * @param sem
 * @return 0 on success
 */
int Sem_Helper_sem_wait(sem_helper_sem_t *sem);

/**
 * Nonblocking semaphore wait
 * @param sem
 * @return 0 on success
 */
int Sem_Helper_sem_trywait(sem_helper_sem_t *sem);


/**
 * Semaphore post
 * @param sem
 * @return 0 on success
 */
int Sem_Helper_sem_post(sem_helper_sem_t *sem);


/**
 * @brief Function that clears out all of the allocated semaphores
 */
void Sem_Helper_clear(void);

/**
 * Cleanup function to call on exit
 */
void Sem_Helper_clean_up(void);


#endif /* SIMPLY_THREAD_SRC_TASK_HELPER_SEM_HELPER_H_ */
