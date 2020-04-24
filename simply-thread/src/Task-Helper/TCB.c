/**
 * @file TCB.c
 * @author Kade Cox
 * @date Created: Mar 9, 2020
 * @details
 *
 */

#include <TCB.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <Sem-Helper.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <execinfo.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifndef MAX_TASK_COUNT
#define MAX_TASK_COUNT 250
#endif //MAX_TASK_COUNT

#ifndef MAX_MSG_COUNT
#define MAX_MSG_COUNT 10
#endif //MAX_MSG_COUNT

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_CYAN, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct tcb_entry_s
{
    tcb_task_t task;
    bool in_use;
}; //!< Structure used in the tcb list

struct tcb_msage_wrapper_s
{
    struct tcb_message_data_s *msg_ptr;
}; //!< The message to send out

struct tcb_module_data_s
{
    struct tcb_entry_s tasks[MAX_TASK_COUNT];
    pthread_t worker_id;
    bool clear_in_progress;
    bool TCB_Executing; //!< Flag that says the task control Block is executing
    pthread_mutex_t clear_mutex; //!< Mutes that provides mutual exclusion for the clear function
}; //!< Structure for the local module data

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct tcb_module_data_s tcb_module_data =
{
    .worker_id = NULL,
    .clear_in_progress = false,
    .TCB_Executing = false,
    .clear_mutex = PTHREAD_MUTEX_INITIALIZER
}; //!< The modules local data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


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

