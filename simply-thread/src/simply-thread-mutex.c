/**
 * @file simply-thread-mutex.c
 * @author Kade Cox
 * @date Created: Mar 25, 2020
 * @details
 *
 */

#include <simply-thread-log.h>
#include <simply-thread-mutex.h>
#include <simply_thread_system_clock.h>
#include <TCB.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX_WAIT_THREADS
#define MAX_WAIT_THREADS 25
#endif //MAX_WAIT_THREADS

#ifndef MAX_NUM_MUTEXES
#define MAX_NUM_MUTEXES 100
#endif //MAX_NUM_MUTEXES

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_ORANGE, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/


struct mutex_wait_task_s
{
    tcb_task_t *waiting_task;  //!< Hold the number of threads waiting on the mutex
    uint64_t count;
    uint64_t max_count;
    sys_clock_on_tick_handle_t tick_handle;
    bool *result;
}; //!<structure that holds all the data pertinent to a waiting task

struct single_mutex_data_s
{
    struct mutex_wait_task_s wait_tasks [MAX_WAIT_THREADS];
    bool available;
} single_mutex_data_s; //!< Structure for a single mutex

struct mutex_module_data_s
{
    Message_Helper_Instance_t *msg_hlper;
    struct
    {
        struct single_mutex_data_s mutex;
        bool  available;
    } all_mutexs[MAX_NUM_MUTEXES];
}; //!< Data for this modules data

struct mutex_create_data_s
{
    simply_thread_mutex_t result;
    const char *name;
}; //!< Structure used to create a mutex

struct mutex_unlock_data_s
{
    bool result;
    tcb_task_t *start_task;
    simply_thread_mutex_t mutex;
}; //!< Structure used to hold the mutex unlock data

struct mutex_lock_data_s
{
    simply_thread_mutex_t mutex;
    unsigned int wait_time;
    tcb_task_t *task;
    bool *result;
}; //!< Structure that holds mutex lock data

struct mutex_on_tick_data_s
{
    sys_clock_on_tick_handle_t handle;
    uint64_t tickval;
    void *args;
}; //!< Structure to use on the mutex on tick message

struct mutex_message_s
{
    enum
    {
        MUTEX_CREATE,
        MUTEX_LOCK,
        MUTEX_UNLOCK,
        MUTEX_ON_TICK
    } type;
    union
    {
        struct mutex_unlock_data_s *unlock;
        struct mutex_create_data_s *create;
        struct mutex_lock_data_s *lock;
        struct mutex_on_tick_data_s *on_tick;
    } data;
    bool *finished;
}; //!< Structure for holding mutex messages


/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct mutex_module_data_s mutex_mod_data =
{
    .msg_hlper = NULL
}; //!< This modules local data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief sent the mutex on tick message
 * @param handle
 * @param tickval
 * @param args
 */
static void mutexlock_on_tick(sys_clock_on_tick_handle_t handle, uint64_t tickval, void *args)
{

    struct mutex_wait_task_s *typed;
    typed = args;
    if(NULL != typed->waiting_task)
    {
        if(typed->count < typed->max_count)
        {
            typed->count++;
            if(typed->count == typed->max_count)
            {
                struct mutex_message_s message;
                struct mutex_on_tick_data_s worker;
                bool comp;
                comp = false;
                worker.args = args;
                worker.handle = handle;
                worker.tickval = tickval;
                message.type = MUTEX_ON_TICK;
                message.finished = &comp;
                message.data.on_tick = &worker;
                Message_Helper_Send(mutex_mod_data.msg_hlper, &message, sizeof(message));
                while(false == comp) {} //Spin untill the message has been handled
            }
        }
    }


}


/**
 * Handle the mutex tick message
 * @param data
 */
static void handle_mutex_on_tick(struct mutex_on_tick_data_s *data)
{
    struct mutex_wait_task_s *typed;
    typed = data->args;
    SS_ASSERT(NULL != typed);

    if(NULL != typed->waiting_task)
    {
        if(SIMPLY_THREAD_TASK_BLOCKED == tcb_get_task_state(typed->waiting_task))
        {
            PRINT_MSG("\tMutex Timed out\r\n");
            tcb_set_task_state(SIMPLY_THREAD_TASK_READY, typed->waiting_task);
            simply_thead_system_clock_deregister_on_tick(data->handle);
            typed->waiting_task = NULL;
        }
    }
}

/**
 * @brief Function that handles the mutex lock function
 * @param data
 */
static void handle_mutex_lock(struct mutex_lock_data_s *data)
{
    struct single_mutex_data_s *mux;

    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    SS_ASSERT(NULL != data);
    mux = data->mutex;
    if(NULL == mux)
    {
        PRINT_MSG("\tMutex is NULL\r\n");
        data->result[0] = false;
        return;
    }
    if(data->wait_time > 0 && NULL == data->task)
    {
        data->wait_time = 0;
    }

    if(mux->available == true)
    {
        PRINT_MSG("\t\tMutex obtained\r\n");
        mux->available =  false;
        PRINT_MSG("\t\tSetting result\r\n");
        data->result[0] = true;
        PRINT_MSG("\t\tResult set\r\n");
    }
    else if(data->wait_time == 0)
    {
        PRINT_MSG("\t\tMutex not available and not blocking\r\n");
        data->result[0] = false;
    }
    else
    {
        PRINT_MSG("\t\tBlock and wait for mutex\r\n");
        bool wait_started  = false;
        SS_ASSERT(NULL != data->task);
        //Add the blocked task to the waiting task list
        for(int i = 0; i < ARRAY_MAX_COUNT(mux->wait_tasks) && false == wait_started; i++)
        {
            if(NULL == mux->wait_tasks[i].waiting_task)
            {
                //block the task
                PRINT_MSG("\t\tSetting task %s to blocked %i\r\n", data->task->name, i);
                tcb_set_task_state(SIMPLY_THREAD_TASK_BLOCKED, data->task);
                mux->wait_tasks[i].count = 0;
                mux->wait_tasks[i].max_count = data->wait_time;
                mux->wait_tasks[i].waiting_task = data->task;
                mux->wait_tasks[i].result = data->result;
                mux->wait_tasks[i].result[0] = false;
                mux->wait_tasks[i].tick_handle = simply_thead_system_clock_register_on_tick(mutexlock_on_tick, &mux->wait_tasks[i]);
                PRINT_MSG("\t\tWait Started\r\n");
                wait_started = true;
            }
        }
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * Function that fetches the next task to start;
 * @param mux
 * @return
 */
static inline tcb_task_t *fetch_start_task(struct single_mutex_data_s *mux)
{
    tcb_task_t *rv;
    int used_index = 0xFFFFFFFF;
    rv = NULL;
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    for(int i = 0; i < ARRAY_MAX_COUNT(mux->wait_tasks); i++)
    {
        if(NULL != mux->wait_tasks[i].waiting_task)
        {
            if(SIMPLY_THREAD_TASK_BLOCKED != mux->wait_tasks[i].waiting_task->state)
            {
                PRINT_MSG("\t\tTask %s State is %i\r\n", mux->wait_tasks[i].waiting_task->name, mux->wait_tasks[i].waiting_task->state);
            }
            SS_ASSERT(SIMPLY_THREAD_TASK_BLOCKED == mux->wait_tasks[i].waiting_task->state);
            if(NULL == rv)
            {
                rv = mux->wait_tasks[i].waiting_task;
                used_index = i;
            }
            else
            {
                if(mux->wait_tasks[i].waiting_task->priority > rv->priority)
                {
                    rv = mux->wait_tasks[i].waiting_task;
                    used_index = i;
                }
            }
        }
    }
    if(NULL != rv)
    {
        simply_thead_system_clock_deregister_on_tick(mux->wait_tasks[used_index].tick_handle);
        mux->wait_tasks[used_index].waiting_task = NULL;
        mux->wait_tasks[used_index].result[0] = true;
    }
    PRINT_MSG("\t%s Finishing with %p\r\n", __FUNCTION__, rv);
    return rv;
}

/**
 * Unlock a mutex from the tcb context
 * @param data
 */
static void handle_mutex_unlock(struct mutex_unlock_data_s *data)
{
    struct single_mutex_data_s *mux;
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    SS_ASSERT(NULL != data);
    mux = data->mutex;
    PRINT_MSG("\t\tWorking with mutex at %p\r\n", mux);
    mux->available = false;
    data->result = false;
    data->start_task = NULL;
    if(NULL != mux)
    {
        SS_ASSERT(false == mux->available);
        data->start_task = fetch_start_task(mux);
        PRINT_MSG("\t\tStart Task Now %p\r\n", data->start_task);
        mux->available = true;
        if(NULL != data->start_task)
        {
            PRINT_MSG("\t\tSetting task %s to ready\r\n", data->start_task->name);
            tcb_set_task_state(SIMPLY_THREAD_TASK_READY, data->start_task);
        }
        else
        {
            PRINT_MSG("\t\tMutex is now available\r\n");
            mux->available = true; //The mutex is available
        }
    }
    data->result = true;
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * Create a mutex from the task control block context
 * @param data
 */
static void handle_mutex_create(struct mutex_create_data_s *data)
{
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    SS_ASSERT(NULL != data);
    for(int i = 0; i < ARRAY_MAX_COUNT(mutex_mod_data.all_mutexs) && NULL == data->result; i++)
    {
        if(true == mutex_mod_data.all_mutexs[i].available)
        {
            data->result = &mutex_mod_data.all_mutexs[i].mutex;
            mutex_mod_data.all_mutexs[i].mutex.available = true;
            for(int j = 0; j < ARRAY_MAX_COUNT(mutex_mod_data.all_mutexs[i].mutex.wait_tasks); j++)
            {
                mutex_mod_data.all_mutexs[i].mutex.wait_tasks[j].waiting_task = NULL;
            }
            mutex_mod_data.all_mutexs[i].available = false;
        }
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Function that handles the mutex messages
 * @param message
 * @param message_size
 */
static void mutex_message_handler(void *message, uint32_t message_size)
{
    struct mutex_message_s typed;
    SS_ASSERT(sizeof(typed) == message_size);
    SS_ASSERT(NULL != message);
    memcpy(&typed, message, message_size);
    switch(typed.type)
    {
        case MUTEX_CREATE:
            handle_mutex_create(typed.data.create);
            typed.finished[0] = true;
            break;
        case MUTEX_LOCK:
            handle_mutex_lock(typed.data.lock);
            typed.finished[0] = true;
            break;
        case MUTEX_UNLOCK:
            handle_mutex_unlock(typed.data.unlock);
            typed.finished[0] = true;
            break;
        case MUTEX_ON_TICK:
            handle_mutex_on_tick(typed.data.on_tick);
            typed.finished[0] = true;
            break;
        default:
            SS_ASSERT(true == false);
    }
}

/**
 * Function that initializes the module from the TCB context
 * @param data
 */
static void tcb_mutex_init(void *data)
{
    if(NULL == mutex_mod_data.msg_hlper)
    {
        PRINT_MSG("%s Running\r\n", __FUNCTION__);
        for(int i = 0; i < ARRAY_MAX_COUNT(mutex_mod_data.all_mutexs); i++)
        {
            mutex_mod_data.all_mutexs[i].available = true;
        }
        mutex_mod_data.msg_hlper = New_Message_Helper(mutex_message_handler, "Mutex-Handler");
        PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    }
}

/**
 * @brief Function that initializes the module if required
 */
static void simply_thread_mutex_init(void)
{
    if(NULL == mutex_mod_data.msg_hlper)
    {
        run_in_tcb_context(tcb_mutex_init, NULL);
    }
}

/**
 * @brief Function that cleans up the simply thread mutexes
 */
void simply_thread_mutex_cleanup(void)
{
    mutex_mod_data.msg_hlper = NULL;
}



/**
 * @brief Function that creates a mutex
 * @param name The name of the mutex
 * @return NULL on error.  Otherwise the mutex handle
 */
simply_thread_mutex_t simply_thread_mutex_create(const char *name)
{
    struct mutex_message_s message;
    struct mutex_create_data_s worker;
    bool comp;

    PRINT_MSG("%s Running\r\n", __FUNCTION__);

    simply_thread_mutex_init();

    if(NULL == name)
    {
        PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
        return NULL;
    }

    comp = false;
    worker.name = name;
    worker.result = NULL;
    message.type = MUTEX_CREATE;
    message.data.create = &worker;
    message.finished = &comp;
    Message_Helper_Send(mutex_mod_data.msg_hlper, &message, sizeof(message));
    while(false == comp) {} //Spin untill the message has been handled
    PRINT_MSG("\tCreated mutex at %p\r\n", worker.result);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return worker.result;
}



/**
 * @brief Function that unlocks a mutex
 * @param mux the mutex handle in question
 * @return true on success
 */
bool simply_thread_mutex_unlock(simply_thread_mutex_t mux)
{
    struct mutex_message_s message;
    struct mutex_unlock_data_s worker;
    bool comp;

    PRINT_MSG("%s Running\r\n", __FUNCTION__);

    simply_thread_mutex_init();

    if(NULL == mux)
    {
        PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
        return false;
    }

    worker.mutex = mux;
    worker.result = false;
    comp = false;
    message.type = MUTEX_UNLOCK;
    message.finished = &comp;
    message.data.unlock = &worker;
    Message_Helper_Send(mutex_mod_data.msg_hlper, &message, sizeof(message));
    while(false == comp) {} //Spin untill the message has been handled
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return worker.result;
}

/**
 * @brief Function that locks a mutex
 * @param mux handle of the mutex to lock
 * @param wait_time How long to wait to obtain a lock
 * @return true on success
 */
bool simply_thread_mutex_lock(simply_thread_mutex_t mux, unsigned int wait_time)
{
    struct mutex_message_s message;
    struct mutex_lock_data_s worker;
    bool comp;
    bool result;

    PRINT_MSG("%s Running\r\n", __FUNCTION__);

    simply_thread_mutex_init();
    comp = false;
    worker.result = &result;
    worker.mutex = mux;
    worker.task = tcb_task_self();
    worker.wait_time = wait_time;
    message.type = MUTEX_LOCK;
    message.finished = &comp;
    message.data.lock = &worker;
    Message_Helper_Send(mutex_mod_data.msg_hlper, &message, sizeof(message));
    while(false == comp) {} //Spin untill the message has been handled
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return worker.result[0];
}
