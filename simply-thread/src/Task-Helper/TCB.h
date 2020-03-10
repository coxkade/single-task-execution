/**
 * @file TCB.h
 * @author Kade Cox
 * @date Created: Mar 9, 2020
 * @details
 * Module that manages the task control block
 */

#ifndef SIMPLY_THREAD_SRC_TASK_HELPER_TCB_H_
#define SIMPLY_THREAD_SRC_TASK_HELPER_TCB_H_

#include <simply-thread.h>
#include <priv-simply-thread.h>
#include <Thread-Helper.h>
#include <stdbool.h>

#ifndef MAX_TASK_DATA_SIZE
#define MAX_TASK_DATA_SIZE 500
#endif //MAX_TASK_DATA_SIZE

typedef struct tcb_task_t
{
    helper_thread_t *thread;
    unsigned int priority;
    uint16_t data_size;
    const char *name;
    uint8_t *data;
    simply_thread_task_fnct cb;
    enum simply_thread_thread_state_e state;
    bool continue_on_run;
} tcb_task_t;


/**
 * reset the task control block data
 */
void tcb_reset(void);

/**
 * @brief Function changes a tasks state
 * @param state
 * @param task
 */
void tcb_set_task_state(enum simply_thread_thread_state_e state, tcb_task_t *task);

/**
 * @brief Function that creates a new task
 * @param name
 * @param cb
 * @param priority
 * @param data
 * @param data_size
 * @return
 */
tcb_task_t *tcb_create_task(const char *name, simply_thread_task_fnct cb, unsigned int priority, void *data, uint16_t data_size);

/**
 * @brief Fetch the task of the calling task
 * @return NULL if the task is not in the TCB
 */
tcb_task_t *tcb_task_self(void);

/**
 * @brief Blocking function that runs a function in the task control block context.
 * @param fnct The function to run
 * @param data the data for the function
 */
void run_in_tcb_context(void (*fnct)(void *), void *data);

#endif /* SIMPLY_THREAD_SRC_TASK_HELPER_TCB_H_ */
