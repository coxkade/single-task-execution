/**
 * @file Thread-Helper.c
 * @author Kade Cox
 * @date Created: Mar 9, 2020
 * @details
 *
 */

#include <Thread-Helper.h>
#include <priv-simply-thread.h>
#include <simply-thread-log.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

//#define DEBUG_THREAD_HELPER

#ifdef DEBUG_THREAD_HELPER
#define PRINT_MSG(...) simply_thread_log(COLOR_MAGENTA, __VA_ARGS__)
#define PRINT_COLOR_MSG(C, ...) simply_thread_log(C, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#define PRINT_COLOR_MSG(C, ...)
#endif //DEBUG_THREAD_HELPER

#ifndef MAX_THREAD_COUNT
#define MAX_THREAD_COUNT 100
#endif //MAX_THREAD_COUNT

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct thread_helper_module_data_s
{
    bool signals_initialized;
    struct sigaction pause_action;
    struct sigaction kill_action;
    helper_thread_t *registry[MAX_THREAD_COUNT];
    int pause_count;
    bool initialized;
    bool pause_enabled;
};

struct self_fetch_data_s
{
    helper_thread_t *result;
    pthread_t id;
}; //!<Structure used for the self fetch data

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/**
 * @brief Function for catching the pause signal
 * @param signo
 */
static void m_catch_pause(int signo);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct thread_helper_module_data_s thread_helper_data =
{
    .signals_initialized = false,
    .pause_count = 0,
    .initialized = false,
    .pause_enabled = false
}; //!<< This modules local data



/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief initialize this module if required
 */
static void init_if_needed(void)
{
    if(false == thread_helper_data.initialized)
    {
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry); i++)
        {
            thread_helper_data.registry[i] = NULL;
        }
        thread_helper_data.pause_count = 0;
        thread_helper_data.initialized = true;
    }

}

/**
 * Function to call from the TCB Context to register a thread
 * @param data
 */
static void register_thread(void *data)
{
    bool finished = false;
    helper_thread_t   *typed;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    typed = data;
    SS_ASSERT(NULL != typed);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry) && false == finished; i++)
    {
        if(NULL == thread_helper_data.registry[i])
        {
            thread_helper_data.registry[i] = typed;
            finished = true;
        }
    }
    SS_ASSERT(true == finished);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * Function to call from the TCB Context to register a thread
 * @param data
 */
static void deregister_thread(void *data)
{
    helper_thread_t   *typed;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    typed = data;
    SS_ASSERT(NULL != typed);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry); i++)
    {
        if(typed == thread_helper_data.registry[i])
        {
            thread_helper_data.registry[i] = NULL;
        }
    }
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * Internal Function for fetching thread handles.  Must only be called with the TCB context
 * @param id
 * @return NULL if not found otherwise the helper thread
 */
static helper_thread_t *internal_get_self(pthread_t id)
{
    helper_thread_t *rv;
    rv = NULL;
    SS_ASSERT(true == tcb_context_executing());
    PRINT_MSG("\tSearching for thread by ID: %p\r\n", id);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry) && NULL == rv; i++)
    {
        if(NULL != thread_helper_data.registry[i])
        {
            if(id == thread_helper_data.registry[i]->id)
            {
                rv = thread_helper_data.registry[i];
            }
        }
    }
    SS_ASSERT(true == tcb_context_executing());
    return rv;
}

/**
 * Function that finds the self thread in the tcb context
 * @param data
 */
static void get_self_thread(void *data)
{
    struct self_fetch_data_s *typed;
    typed = data;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != typed);
    typed->result = internal_get_self(typed->id);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Function for catching the pause signal
 * @param signo
 */
static void m_catch_pause(int signo)
{
    PRINT_MSG("%s Started %p\r\n", __FUNCTION__, pthread_self());
    int result = -1;
    helper_thread_t *worker;
    sigset_t waiting_mask;
    SS_ASSERT(PAUSE_SIGNAL == signo);
    SS_ASSERT(0 == sigpending(&waiting_mask));
    if(sigismember(&waiting_mask, PAUSE_SIGNAL))
    {
        PRINT_MSG("\t%s PAUSE_SIGNAL is pending\r\n", __FUNCTION__);
    }
    PRINT_MSG("\t%s Fetching the worker\r\n", __FUNCTION__);
    worker = internal_get_self(pthread_self());


    SS_ASSERT(NULL != worker);
    if(false == worker->pause_requested)
    {
        PRINT_MSG("\tExtra Pause signal caught\r\n");
        return; //Pause not wanted bail
    }

    worker->pause_requested = false;
    worker->thread_running = false;
    PRINT_MSG("\tThread No Longer Running\r\n");
    while(0 != result)
    {
        PRINT_MSG("\t%s waiting on sem %i\r\n", __FUNCTION__, worker->wait_sem.id);
        result = simply_thread_sem_wait(&worker->wait_sem);
        if(0 != result)
        {
            PRINT_COLOR_MSG(COLOR_PURPLE, "---m_catch_pause returned %i\r\n", result);
        }
    }
    worker->thread_running = true;
    PRINT_MSG("\tThread Now Running\r\n");
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Function that catches the kill message
 * @param signo
 */
static void m_catch_kill(int signo)
{
    PRINT_MSG("%s Started %p\r\n", __FUNCTION__, pthread_self());
    SS_ASSERT(KILL_SIGNAL == signo);
    PRINT_MSG("\t%s Calling pthread_exit\r\n", __FUNCTION__);
    pthread_exit(NULL);
}

/**
 * @brief Function that initializes the module if required
 */
static void init_if_required(void)
{
    init_if_needed();
    if(false == thread_helper_data.signals_initialized)
    {
        PRINT_MSG("%s Initializing the module\r\n", __FUNCTION__);
        thread_helper_data.pause_action.sa_handler = m_catch_pause;
        thread_helper_data.pause_action.sa_flags = 0;
        SS_ASSERT(0 == sigaction(PAUSE_SIGNAL, &thread_helper_data.pause_action, NULL));
        thread_helper_data.kill_action.sa_handler = m_catch_kill;
        thread_helper_data.kill_action.sa_flags = 0;
        SS_ASSERT(0 == sigaction(KILL_SIGNAL, &thread_helper_data.kill_action, NULL));
        thread_helper_data.signals_initialized = true;
    }
}

/**
 * @brief The internal worker function for this module
 * @param data
 */
static void *thread_helper_worker(void *data)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    helper_thread_t *typed;
    typed = data;
    SS_ASSERT(NULL != typed);
    SS_ASSERT(NULL != typed->worker);
    typed->thread_running = true;
    return typed->worker(typed->worker_data);
}


/**
 * @brief Function that fetches the current thread helper
 * @return NULL if not known
 */
helper_thread_t *thread_helper_self(void)
{
    struct self_fetch_data_s result;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    result.id = pthread_self();
    run_in_tcb_context(get_self_thread, &result);
    return result.result;
}

/**
 * @brief Create and start a new thread
 * @param worker Function used with the thread
 * @param data the data used with the thread
 * @return Ptr to the new thread object
 */
helper_thread_t *thread_helper_thread_create(void *(* worker)(void *), void *data)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    helper_thread_t *rv;
    SS_ASSERT(NULL != worker);
    init_if_required();
    rv = malloc(sizeof(helper_thread_t));
    SS_ASSERT(NULL != rv);
    rv->thread_running = false;
    rv->worker = worker;
    rv->worker_data = data;
    rv->pause_requested = false;
    PRINT_MSG("\t%s initializing semaphore\r\n", __FUNCTION__);
    simply_thread_sem_init(&rv->wait_sem);
    PRINT_MSG("\t%s checking semaphore\r\n", __FUNCTION__);
    SS_ASSERT(EAGAIN == simply_thread_sem_trywait(&rv->wait_sem));
    run_in_tcb_context(register_thread, rv);
    PRINT_MSG("\t%s Launching the thread\r\n", __FUNCTION__);
    SS_ASSERT(0 == pthread_create(&rv->id, NULL, thread_helper_worker, rv));
    while(false == rv->thread_running) {}
    SS_ASSERT(true == rv->thread_running);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief Destroy a previously created thread.  This thread blocks
 * @param thread
 */
void thread_helper_thread_destroy(helper_thread_t *thread)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    init_if_required();
    SS_ASSERT(NULL != thread);
    PRINT_MSG("\t%s %p Killing thread %p\r\n", __FUNCTION__, pthread_self(), thread->id);
    SS_ASSERT(0 == pthread_kill(thread->id, KILL_SIGNAL));
    PRINT_MSG("\t%s Joining thread %p\r\n", __FUNCTION__, thread->id);
    pthread_join(thread->id, NULL);
    PRINT_MSG("\t%s Deregistering thread\r\n", __FUNCTION__);
    run_in_tcb_context(deregister_thread, thread);
    PRINT_MSG("\t%s Destroying the semaphore\r\n", __FUNCTION__);
    simply_thread_sem_destroy(&thread->wait_sem);
    free(thread);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * Function that destroys thread from the assert context
 * @param thread
 */
void thread_helper_thread_assert_destroy(helper_thread_t *thread)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    init_if_required();
    SS_ASSERT(NULL != thread);
    PRINT_MSG("\t%s %p Killing thread %p\r\n", __FUNCTION__, pthread_self(), thread->id);
    SS_ASSERT(0 == pthread_kill(thread->id, KILL_SIGNAL));
    PRINT_MSG("\t%s Joining thread %p\r\n", __FUNCTION__, thread->id);
    pthread_join(thread->id, NULL);
    PRINT_MSG("\t%s Deregistering thread\r\n", __FUNCTION__);
    deregister_thread(thread);
    PRINT_MSG("\t%s Destroying the semaphore\r\n", __FUNCTION__);
    simply_thread_sem_destroy(&thread->wait_sem);
    free(thread);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Check if a thread is running
 * @param thread The thread to check
 * @return True if the thread is running.  False otherwise.
 */
bool thread_helper_thread_running(helper_thread_t *thread)
{
    bool rv;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    init_if_required();
    SS_ASSERT(NULL != thread);
    rv = thread->thread_running;
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * Pause a function from the tcb context
 * @param data
 */
static void thread_helper_pause_from_tcb(void *data)
{
    helper_thread_t *thread;
    thread = data;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != thread);
    SS_ASSERT(false == thread->pause_requested);
    thread->pause_requested = true;
    PRINT_MSG("\tTriggering the PAUSE_SIGNAL\r\n");
    SS_ASSERT(0 == pthread_kill(thread->id, PAUSE_SIGNAL));
    while(true == thread->thread_running) {}
    SS_ASSERT(false == thread->thread_running);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    thread_helper_data.pause_count++;
}

/**
 * @brief Pause a thread
 * @param thread
 */
void thread_helper_pause_thread(helper_thread_t *thread)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    init_if_required();
    SS_ASSERT(thread->id != pthread_self());
    run_in_tcb_context(thread_helper_pause_from_tcb, thread);
}


/**
 * @brief unpause a task from the task control context
 * @param data
 */
static void thread_helper_run_from_tcb(void *data)
{
    sigset_t waiting_mask;
    helper_thread_t *thread;
    thread = data;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != thread);
    SS_ASSERT(thread->id != pthread_self());
    SS_ASSERT(false == thread->thread_running);
    PRINT_MSG("\t%s resuming thread %i\r\n", __FUNCTION__, thread->wait_sem.id);
    SS_ASSERT(0 == simply_thread_sem_post(&thread->wait_sem));
    while(false == thread->thread_running) {}
    SS_ASSERT(0 == sigpending(&waiting_mask));
    if(sigismember(&waiting_mask, PAUSE_SIGNAL))
    {
        PRINT_MSG("\t%s PAUSE_SIGNAL is pending\r\n", __FUNCTION__);
    }
    SS_ASSERT(true == thread->thread_running);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    thread_helper_data.pause_count--;
}

/**
 * @brief Run a thread
 * @param thread
 */
void thread_helper_run_thread(helper_thread_t *thread)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    init_if_required();
    run_in_tcb_context(thread_helper_run_from_tcb, thread);
}

/**
 * @brief get the id of the worker thread
 * @param thread
 * @return the thread id
 */
pthread_t thread_helper_get_id(helper_thread_t *thread)
{
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    init_if_needed();
    SS_ASSERT(NULL != thread);
    return thread->id;
}

/**
 * @brief Reset the thread helper
 */
void reset_thread_helper(void)
{
    thread_helper_data.initialized = false;
    init_if_needed();
}
