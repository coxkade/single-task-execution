/**
 * @file TCB.c
 * @author Kade Cox
 * @date Created: Mar 9, 2020
 * @details
 *
 */

#include <TCB.h>
#include <Message-Helper.h>
#include <simply-thread-sem-helper.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

struct tcb_message_data_s
{
    simply_thread_sem_t wait_sem;
    enum
    {
        TCB_SET_STATE,
		TCB_GET_STATE,
        TCB_CREATE_TASK,
        TCB_TASK_SELF,
        TCB_RUNNER
    } msg_type; //!< The type of message being sent
    union
    {
        struct
        {
            struct tcb_task_t *task;
            enum simply_thread_thread_state_e state;
        } set_state; //!< Data for the set state command
        struct
        {
            struct tcb_task_t *task;
            enum simply_thread_thread_state_e * state;
        } get_state; //!< Data for the get state command
        struct
        {
            const char *name;
            simply_thread_task_fnct cb;
            unsigned int priority;
            void *data;
            uint16_t data_size;
            struct
            {
                tcb_task_t *tcb_task;
            } result;
        } create_task; //!< Data for the create task command
        struct
        {
            pthread_t self_id;
            struct
            {
                tcb_task_t *tcb_task;
            } result;
        } task_self; //!< Data for the task self command
        struct
        {
            void (*fnct)(void *);
            void *data;
        } runner; //!< Data for the tcb function runner
    } msg_data; //!< Union that holds the message data
}; //!<Structure that holds the data of the message pointer

struct tcb_msage_wrapper_s
{
    struct tcb_message_data_s *msg_ptr;
}; //!< The message to send out

struct tcb_module_data_s
{
    struct tcb_entry_s tasks[MAX_TASK_COUNT];
    Message_Helper_Instance_t *msg_helper;
    pthread_t worker_id;
    bool clear_in_progress;
}; //!< Structure for the local module data

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct tcb_module_data_s tcb_module_data =
{
    .msg_helper = NULL,
    .worker_id = NULL,
	.clear_in_progress = false
};

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

static void tcb_on_exit(void)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    if(NULL != tcb_module_data.msg_helper)
    {
        Remove_Message_Helper(tcb_module_data.msg_helper);
    }
}

/**
 * @brief Function that runs the scheduler
 */
static void tcb_run_sched(void)
{
    struct tcb_entry_s *c_task;
    struct tcb_entry_s *best_task;
    PRINT_MSG("Running the Scheduler\r\n");
    //first pause all tasks
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks); i++)
    {
        c_task = &tcb_module_data.tasks[i];
        if(true == c_task->in_use && c_task->task.state == SIMPLY_THREAD_TASK_RUNNING)
        {
            //The task is running so we need to suspend the task
            SS_ASSERT(true == thread_helper_thread_running(c_task->task.thread));
            thread_helper_pause_thread(c_task->task.thread);
            c_task->task.state = SIMPLY_THREAD_TASK_READY;
        }
    }
    //Now find the best task to start
    best_task = NULL;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks); i++)
    {
        c_task = &tcb_module_data.tasks[i];
        if(true == c_task->in_use && SIMPLY_THREAD_TASK_READY == c_task->task.state)
        {
            SS_ASSERT(false == thread_helper_thread_running(c_task->task.thread));
            //This is a ready task.
            if(NULL == best_task)
            {
                //This is the first ready task that we have found
                best_task = c_task;
            }
            else
            {
                //See if the new tasks priority is higher
                if(best_task->task.priority < c_task->task.priority)
                {
                    best_task = c_task; //Found higher priority task
                }
            }
        }
    }
    if(NULL != best_task)
    {
        //Start the new task
        PRINT_MSG("TCB starting task: %s\r\n", best_task->task.name);
        best_task->task.state = SIMPLY_THREAD_TASK_RUNNING;
        thread_helper_run_thread(best_task->task.thread);
        SS_ASSERT(true == thread_helper_thread_running(best_task->task.thread));
    }
}

/**
 * The Main Task Runner Function
 * @param data
 */
static void *task_runner_function(void *data)
{
    struct tcb_entry_s *tcb_entry;
    tcb_entry = data;
    SS_ASSERT(NULL != tcb_entry);
    SS_ASSERT(true == tcb_entry->in_use);
    while(false == tcb_entry->task.continue_on_run) {}
    SS_ASSERT(true == tcb_entry->task.continue_on_run);
    SS_ASSERT(NULL != tcb_entry->task.cb);
    //Ok Now actually run the task
    if(0 == tcb_entry->task.data_size)
    {
        tcb_entry->task.cb(NULL, 0);
    }
    else
    {
        tcb_entry->task.cb(tcb_entry->task.data, tcb_entry->task.data_size);
    }

    return NULL;
}

/**
 * @brief Fetch an available task control block
 * @return NULL on error
 */
static struct tcb_entry_s *get_available_tcb_entry(void)
{
    struct tcb_entry_s *rv = NULL;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks) && NULL == rv; i++)
    {
        if(false == tcb_module_data.tasks[i].in_use)
        {
            rv = &tcb_module_data.tasks[i];
        }
    }
    SS_ASSERT(NULL != rv);
    return rv;
}

/**
 * Function that handles the task create message
 * @param msg
 */
static void handle_task_create_msg(struct tcb_message_data_s *msg)
{
    struct tcb_entry_s *tcb_entry;
    SS_ASSERT(TCB_CREATE_TASK == msg->msg_type);
    tcb_entry = get_available_tcb_entry();
    tcb_entry->in_use = true;
    tcb_entry->task.state = SIMPLY_THREAD_TASK_READY;
    tcb_entry->task.continue_on_run = false;
    tcb_entry->task.cb = msg->msg_data.create_task.cb;
    tcb_entry->task.name = msg->msg_data.create_task.name;
    tcb_entry->task.data_size = msg->msg_data.create_task.data_size;
    tcb_entry->task.priority = msg->msg_data.create_task.priority;
    if(0 < tcb_entry->task.data_size)
    {
        SS_ASSERT(NULL != msg->msg_data.create_task.data);
    }
    tcb_entry->task.data = msg->msg_data.create_task.data;
    tcb_entry->task.thread = thread_helper_thread_create(task_runner_function, tcb_entry);
    PRINT_MSG("\tTask %s created\r\n", tcb_entry->task.name);
    SS_ASSERT(NULL != tcb_entry->task.thread);
    //Pause the newly created task
    thread_helper_pause_thread(tcb_entry->task.thread);
    //set continue_on_run so that the thread can resume when unpaused
    tcb_entry->task.continue_on_run = true;
    //Set the return value
    msg->msg_data.create_task.result.tcb_task = &tcb_entry->task;
}

/**
 * @brief Function that handles the change state message
 * @param msg The change state message
 */
static void handle_change_state_msg(struct tcb_message_data_s *msg)
{
    enum simply_thread_thread_state_e last_state;
    SS_ASSERT(NULL != msg);
    SS_ASSERT(TCB_SET_STATE == msg->msg_type);

    SS_ASSERT(SIMPLY_THREAD_TASK_RUNNING != msg->msg_data.set_state.state);
    SS_ASSERT(SIMPLY_THREAD_TASK_UNKNOWN_STATE != msg->msg_data.set_state.state);
    SS_ASSERT(SIMPLY_THREAD__TASK_STATE_COUNT > msg->msg_data.set_state.state);
    SS_ASSERT(NULL != msg->msg_data.set_state.task);

    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    //Update the state
    last_state = msg->msg_data.set_state.task->state;
    msg->msg_data.set_state.task->state = msg->msg_data.set_state.state;
    PRINT_MSG("\tTask %s state changed to: %u from %u\r\n", msg->msg_data.set_state.task->name, msg->msg_data.set_state.task->state, last_state);
    if(last_state != msg->msg_data.set_state.task->state)
    {
        if(SIMPLY_THREAD_TASK_RUNNING == last_state)
        {
            PRINT_MSG("\tState was SIMPLY_THREAD_TASK_RUNNING, suspend the task\r\n");
            SS_ASSERT(true == thread_helper_thread_running(msg->msg_data.set_state.task->thread));
            thread_helper_pause_thread(msg->msg_data.set_state.task->thread);
            SS_ASSERT(false == thread_helper_thread_running(msg->msg_data.set_state.task->thread));
        }
    }
}

/**
 * @brief Function that handles the change state message
 * @param msg The change state message
 */
static void handle_get_state_msg(struct tcb_message_data_s *msg)
{
    SS_ASSERT(NULL != msg);
    SS_ASSERT(TCB_GET_STATE == msg->msg_type);
    SS_ASSERT(NULL != msg->msg_data.get_state.state);
    SS_ASSERT(NULL != msg->msg_data.get_state.task);

    msg->msg_data.get_state.state[0] = msg->msg_data.get_state.task->state;
}

/**
 * @brief Function that handles the task self command
 * @param msg
 */
static void handle_task_self(struct tcb_message_data_s *msg)
{
    tcb_task_t *rv = NULL;
    pthread_t thread_id;
    SS_ASSERT(NULL != msg);
    SS_ASSERT(TCB_TASK_SELF == msg->msg_type);

    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks) && NULL == rv; i++)
    {
        if(true == tcb_module_data.tasks[i].in_use)
        {
            thread_id = thread_helper_get_id(tcb_module_data.tasks[i].task.thread);
            if(thread_id == msg->msg_data.task_self.self_id)
            {
                rv = &tcb_module_data.tasks[i].task;
                PRINT_MSG("\tFound the thread entry %p\r\n", rv);
            }
        }
    }
    msg->msg_data.task_self.result.tcb_task = rv;
}

/**
 * Run a function in the tcb context
 * @param msg
 */
static void handle_task_run(struct tcb_message_data_s *msg)
{
    SS_ASSERT(TCB_RUNNER == msg->msg_type);
    msg->msg_data.runner.fnct(msg->msg_data.runner.data);
}

/**
 * @brief the tcb_message_handler
 * @param message
 * @param message_size
 */
static void tcb_message_handler(void *message, uint32_t message_size)
{
    struct tcb_msage_wrapper_s *typed;
    bool sched_required;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);

    tcb_module_data.worker_id = pthread_self();
    typed = message;
    SS_ASSERT(message_size == sizeof(struct tcb_msage_wrapper_s));
    SS_ASSERT(NULL != typed);
    sched_required = true;
    switch(typed->msg_ptr->msg_type)
    {
        case TCB_SET_STATE:
            PRINT_MSG("\tHandling Set State\r\n");
            handle_change_state_msg(typed->msg_ptr);
            break;
        case TCB_GET_STATE:
        	PRINT_MSG("\tHandling Get State\r\n");
            handle_get_state_msg(typed->msg_ptr);
            sched_required = false;
            break;
        case TCB_CREATE_TASK:
            PRINT_MSG("\tHandling Create Task\r\n");
            handle_task_create_msg(typed->msg_ptr);
            break;
        case TCB_TASK_SELF:
            PRINT_MSG("\tHandling Task Self\r\n");
            handle_task_self(typed->msg_ptr);
            sched_required = false;
            break;
        case TCB_RUNNER:
            PRINT_MSG("\tHandling Task Runner\r\n");
            handle_task_run(typed->msg_ptr);
            sched_required = false;
            break;
        default:
            SS_ASSERT(false);
    }
    if(true == sched_required)
    {
        //Now run the scheduler
        tcb_run_sched();
    }
    //post to the wait semaphore
    SS_ASSERT(0 == simply_thread_sem_post(&typed->msg_ptr->wait_sem));
}

//!< Clears all the data in the tcb
static void tcb_clear(void)
{
	struct tcb_entry_s *c_task;
	tcb_module_data.clear_in_progress = true;
	if(NULL != tcb_module_data.msg_helper)
	{
		printf("!!!!!!Running %s\r\n", __FUNCTION__);
		//Clean up is required
		//Kill the messenger
		Remove_Message_Helper(tcb_module_data.msg_helper);
		printf("!!!!!!Removed Message Helper %s\r\n", __FUNCTION__);
		tcb_module_data.msg_helper = NULL;
		//Kill all running tasks
		for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks); i++)
		{
			c_task = &tcb_module_data.tasks[i];
			if(true == c_task->in_use)
			{
				thread_helper_thread_destroy(c_task->task.thread);
			}
		}
	}
	reset_thread_helper();
	tcb_module_data.clear_in_progress = false;
}

/**
 * Function to call when an assert occurs
 */
void tcb_on_assert(void)
{
	tcb_clear();
}

/**
 * reset the task control block data
 */
void tcb_reset(void)
{
    static bool first_time = true;
    struct tcb_entry_s *c_task;

    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    while(true == tcb_module_data.clear_in_progress){}

    if(true == first_time)
    {
        PRINT_MSG("\tSetting up on exit\r\n");
        atexit(tcb_on_exit);
        first_time = false;
    }

    tcb_clear();
    //Initialize the module data
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks); i++)
    {
        c_task = &tcb_module_data.tasks[i];
        c_task->in_use = false;
    }
    //Launch the message handler
    tcb_module_data.msg_helper = New_Message_Helper(tcb_message_handler);
    SS_ASSERT(NULL != tcb_module_data.msg_helper);
}

/**
 * @brief Function changes a tasks state
 * @param state
 * @param task
 */
void tcb_set_task_state(enum simply_thread_thread_state_e state, tcb_task_t *task)
{
    struct tcb_message_data_s local_data;
    struct tcb_msage_wrapper_s msg;

    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    while(true == tcb_module_data.clear_in_progress){}
    SS_ASSERT(tcb_module_data.worker_id != pthread_self());

    SS_ASSERT(SIMPLY_THREAD_TASK_RUNNING != state && NULL != task);
    msg.msg_ptr = &local_data;

    simply_thread_sem_init(&local_data.wait_sem);

    local_data.msg_type = TCB_SET_STATE;
    local_data.msg_data.set_state.state = state;
    local_data.msg_data.set_state.task = task;

    Message_Helper_Send(tcb_module_data.msg_helper, &msg, sizeof(struct tcb_msage_wrapper_s));
    while(0 != simply_thread_sem_wait(&local_data.wait_sem)) {}
    simply_thread_sem_destroy(&local_data.wait_sem);
}

/**
 * @brief Fetch the current state of a task
 * @param task
 * @return the current state of the task
 */
enum simply_thread_thread_state_e tcb_get_task_state(tcb_task_t *task)
{
    struct tcb_message_data_s local_data;
    struct tcb_msage_wrapper_s msg;
    enum simply_thread_thread_state_e rv;

    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    while(true == tcb_module_data.clear_in_progress){}
    SS_ASSERT(tcb_module_data.worker_id != pthread_self());

    SS_ASSERT(NULL != task);
    msg.msg_ptr = &local_data;

    simply_thread_sem_init(&local_data.wait_sem);

    local_data.msg_type = TCB_GET_STATE;
    local_data.msg_data.get_state.task = task;
    local_data.msg_data.get_state.state = &rv;

    Message_Helper_Send(tcb_module_data.msg_helper, &msg, sizeof(struct tcb_msage_wrapper_s));
    while(0 != simply_thread_sem_wait(&local_data.wait_sem)) {}
    simply_thread_sem_destroy(&local_data.wait_sem);
    return rv;
}

/**
 * @brief Function that creates a new task
 * @param name
 * @param cb
 * @param priority
 * @param data
 * @param data_size
 * @return
 */
tcb_task_t *tcb_create_task(const char *name, simply_thread_task_fnct cb, unsigned int priority, void *data, uint16_t data_size)
{
    struct tcb_message_data_s local_data;
    struct tcb_msage_wrapper_s msg;

    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    while(true == tcb_module_data.clear_in_progress){}
    SS_ASSERT(tcb_module_data.worker_id != pthread_self());
    SS_ASSERT(NULL != name && NULL != cb);
    msg.msg_ptr = &local_data;

    simply_thread_sem_init(&local_data.wait_sem);
    local_data.msg_type = TCB_CREATE_TASK;
    local_data.msg_data.create_task.cb = cb;
    local_data.msg_data.create_task.data = data;
    local_data.msg_data.create_task.data_size = data_size;
    local_data.msg_data.create_task.name = name;
    local_data.msg_data.create_task.priority = priority;
    local_data.msg_data.create_task.result.tcb_task = NULL;
    Message_Helper_Send(tcb_module_data.msg_helper, &msg, sizeof(struct tcb_msage_wrapper_s));
    while(0 != simply_thread_sem_wait(&local_data.wait_sem)) {}
    simply_thread_sem_destroy(&local_data.wait_sem);
    return local_data.msg_data.create_task.result.tcb_task;
}

/**
 * @brief Fetch the task of the calling task
 * @return NULL if the task is not in the TCB
 */
tcb_task_t *tcb_task_self(void)
{
    struct tcb_message_data_s local_data;
    struct tcb_msage_wrapper_s msg;

    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    while(true == tcb_module_data.clear_in_progress){}
    SS_ASSERT(tcb_module_data.worker_id != pthread_self());

    msg.msg_ptr = &local_data;
    simply_thread_sem_init(&local_data.wait_sem);

    local_data.msg_type = TCB_TASK_SELF;
    local_data.msg_data.task_self.self_id = pthread_self();
    local_data.msg_data.task_self.result.tcb_task = NULL;

    Message_Helper_Send(tcb_module_data.msg_helper, &msg, sizeof(struct tcb_msage_wrapper_s));
    while(0 != simply_thread_sem_wait(&local_data.wait_sem)) {}
    simply_thread_sem_destroy(&local_data.wait_sem);
    return(local_data.msg_data.task_self.result.tcb_task);
}


/**
 * @brief Blocking function that runs a function in the task control block context.
 * @param fnct The function to run
 * @param data the data for the function
 */
void run_in_tcb_context(void (*fnct)(void *), void *data)
{
    struct tcb_message_data_s local_data;
    struct tcb_msage_wrapper_s msg;

    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    while(true == tcb_module_data.clear_in_progress){}
    SS_ASSERT(tcb_module_data.worker_id != pthread_self());

    msg.msg_ptr = &local_data;
    simply_thread_sem_init(&local_data.wait_sem);

    local_data.msg_type = TCB_RUNNER;
    local_data.msg_data.runner.fnct = fnct;
    local_data.msg_data.runner.data = data;

    Message_Helper_Send(tcb_module_data.msg_helper, &msg, sizeof(struct tcb_msage_wrapper_s));
    while(0 != simply_thread_sem_wait(&local_data.wait_sem)) {}
    simply_thread_sem_destroy(&local_data.wait_sem);
}
