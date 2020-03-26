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
 * @brief Fetch the current state of a task
 * @param task
 * @return the current state of the task
 */
enum simply_thread_thread_state_e tcb_get_task_state(tcb_task_t *task);


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

/**
 * Function to call when an assert occurs
 */
void tcb_on_assert(void);

/**
 * @brief Function to check and see if the task control block is executing
 * @return True it the TCB context is running an opperation
 */
bool tcb_context_executing(void);

/**
 * @brief Function that tells if we are executing from the TCB context
 * @return true if the calling function is in the TCB Context
 */
bool in_tcb_context(void);

#endif /* SIMPLY_THREAD_SRC_TASK_HELPER_TCB_H_ */