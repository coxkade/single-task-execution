/**
 * @file Thread-Helper.h
 * @author Kade Cox
 * @date Created: Mar 9, 2020
 * @details
 *
 */

#ifndef SIMPLY_THREAD_SRC_TASK_HELPER_THREAD_HELPER_H_
#define SIMPLY_THREAD_SRC_TASK_HELPER_THREAD_HELPER_H_

#include <pthread.h>
#include <stdbool.h>
#include <signal.h>
#include <Sem-Helper.h>

#define PAUSE_SIGNAL SIGUSR1
#define KILL_SIGNAL SIGUSR2

typedef struct helper_thread_t
{
    bool thread_running;
    pthread_t id;
    void *(* worker)(void *);
    void *worker_data;
    sem_helper_sem_t wait_sem;
} helper_thread_t; //!< Helper thread data

/**
 * @brief Function that fetches the current thread helper
 * @return NULL if not known
 */
helper_thread_t *thread_helper_self(void);

/**
 * @brief Create and start a new thread
 * @param worker Function used with the thread
 * @param data the data used with the thread
 * @return Ptr to the new thread object
 */
helper_thread_t *thread_helper_thread_create(void *(* worker)(void *), void *data);

/**
 * @brief Destroy a previously created thread.  This thread blocks
 * @param thread
 */
void thread_helper_thread_destroy(helper_thread_t *thread);

/**
 * Function that destroys thread from the assert context
 * @param thread
 */
void thread_helper_thread_assert_destroy(helper_thread_t *thread);

/**
 * @brief Check if a thread is running
 * @param thread The thread to check
 * @return True if the thread is running.  False otherwise.
 */
bool thread_helper_thread_running(helper_thread_t *thread);

/**
 * @brief Pause a thread
 * @param thread
 */
void thread_helper_pause_thread(helper_thread_t *thread);

/**
 * Have a thread enter a critical section
 */
void thread_exit_critical_section(helper_thread_t *thread);

/**
 * @brief Run a thread
 * @param thread
 */
void thread_helper_run_thread(helper_thread_t *thread);

/**
 * @brief get the id of the worker thread
 * @param thread
 * @return the thread id
 */
pthread_t thread_helper_get_id(helper_thread_t *thread);

/**
 * @brief Reset the thread helper
 */
void reset_thread_helper(void);

/**
 * Function to call when program exits to clean up all the hanging thread helper data
 */
void thread_helper_cleanup(void);


#endif /* SIMPLY_THREAD_SRC_TASK_HELPER_THREAD_HELPER_H_ */
