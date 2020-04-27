/**
 * @file Thread-Helper.c
 * @author Kade Cox
 * @date Created: Mar 9, 2020
 * @details
 *
 */

#include <Thread-Helper.h>
#include <simply-thread.h>
#include <simply-thread-log.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <que-creator.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifdef DEBUG_THREAD_HELPER
#define PRINT_MSG(...) simply_thread_log(COLOR_MAGENTA, __VA_ARGS__)
//#define PRINT_MSG(...) printf(__VA_ARGS__)
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
    bool initialized;
    struct sigaction pause_action;
    struct sigaction kill_action;
    struct
    {
        helper_thread_t thread;
        bool in_use;
    } registry[MAX_THREAD_COUNT];
}; //!< structure that defines data for the thread helper module



/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/**
 * @brief Function for catching the pause signal
 * @param signo
 */
static void m_catch_pause(int signo);

/**
 * @brief Function that catches the kill message
 * @param signo
 */
static void m_catch_kill(int signo);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct thread_helper_module_data_s thread_helper_data =
{
    .signals_initialized = false,
    .initialized = false
}; //!<< This modules local data



/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

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
    worker = thread_helper_self();

    SS_ASSERT(NULL != worker);

    worker->thread_running = false;
    PRINT_MSG("\tThread No Longer Running\r\n");
    while(0 != result)
    {
        PRINT_MSG("\t%s waiting on sem %i\r\n", __FUNCTION__, worker->wait_sem.id);
        result = Sem_Helper_sem_wait(&worker->wait_sem);
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
 * @brief initialize this module if required
 */
static void init_if_needed(void)
{
    if(false == thread_helper_data.initialized)
    {
        PRINT_MSG("%s Running\r\n", __FUNCTION__);
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry); i++)
        {
            thread_helper_data.registry[i].in_use = false;
        }
        thread_helper_data.initialized = true;
    }
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
 * @brief Internal function that kills a single thread
 * @param index the index of the tread to destroy
 */
static void thread_helper_destroy_index(int index)
{
    helper_thread_t *thread;

    thread = &thread_helper_data.registry[index].thread;

    PRINT_MSG("\tDestroying thread at %i %p\r\n", index, thread);
    pthread_kill(thread->id, KILL_SIGNAL);
    pthread_join(thread->id, NULL);
    thread_helper_data.registry[index].in_use = false;
}

/**
 * The internal thread runner
 * @param data
 */
static void *thread_helper_runner(void *data)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    helper_thread_t *typed;
    typed = data;
    SS_ASSERT(NULL != typed);
    typed->thread_running = true;
    return typed->worker(typed->worker_data);
}

/**
 * @brief Function that fetches the current thread helper
 * @return NULL if not known
 */
helper_thread_t *thread_helper_self(void)
{
    helper_thread_t *rv;
    pthread_t me;
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_needed();
    rv = NULL;
    me = pthread_self();
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry) && NULL == rv; i++)
    {
        if(true == thread_helper_data.registry[i].in_use && me == thread_helper_data.registry[i].thread.id)
        {
            rv = &thread_helper_data.registry[i].thread;
        }
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief Create and start a new thread
 * @param worker Function used with the thread
 * @param data the data used with the thread
 * @return Ptr to the new thread object
 */
helper_thread_t *thread_helper_thread_create(void *(* worker)(void *), void *data)
{
    helper_thread_t *rv;
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_needed();
    rv = NULL;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry) && NULL == rv; i++)
    {
        if(false == thread_helper_data.registry[i].in_use)
        {
            thread_helper_data.registry[i].in_use = true;
            rv = &thread_helper_data.registry[i].thread;
        }
    }

    SS_ASSERT(NULL != rv);
    rv->thread_running = false;
    Sem_Helper_sem_init(&rv->wait_sem);
    rv->worker = worker;
    rv->worker_data = data;
    SS_ASSERT(0 == pthread_create(&rv->id, NULL, thread_helper_runner, rv));
    while(false == rv->thread_running)
    {

    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief Destroy a previously created thread.  This thread blocks
 * @param thread
 */
void thread_helper_thread_destroy(helper_thread_t *thread)
{
    bool destroyed;
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_needed();
    destroyed = false;

    //We are not allowed to destroy ourself
    SS_ASSERT(NULL != thread);
    SS_ASSERT(thread->id != pthread_self());

    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry) && false == destroyed; i++)
    {
        if(true == thread_helper_data.registry[i].in_use && thread == &thread_helper_data.registry[i].thread)
        {
            thread_helper_destroy_index(i);
            destroyed = true;
        }
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}


/**
 * @brief Check if a thread is running
 * @param thread The thread to check
 * @return True if the thread is running.  False otherwise.
 */
bool thread_helper_thread_running(helper_thread_t *thread)
{
    bool rv;
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_needed();
    rv = false;
    if(NULL != thread)
    {
        rv = thread->thread_running;
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief Pause a thread
 * @param thread
 */
void thread_helper_pause_thread(helper_thread_t *thread)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_needed();

    //We are not allowed to pause ourself
    SS_ASSERT(NULL != thread);
    SS_ASSERT(thread->id != pthread_self());
    //We must not be already paused
    SS_ASSERT(true == thread->thread_running);

    pthread_kill(thread->id, PAUSE_SIGNAL);
    //Wait till the thread is paused
    while(true == thread->thread_running) {}

    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Run a thread
 * @param thread
 */
void thread_helper_run_thread(helper_thread_t *thread)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_needed();

    //We are not allowed to run ourself
    SS_ASSERT(NULL != thread);
    SS_ASSERT(thread->id != pthread_self());

    if(false == thread->thread_running)
    {
        SS_ASSERT(0 == Sem_Helper_sem_post(&thread->wait_sem));
        //wait for thread to be running
        while(false == thread->thread_running) {}
    }

    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief get the id of the worker thread
 * @param thread
 * @return the thread id
 */
pthread_t thread_helper_get_id(helper_thread_t *thread)
{
    pthread_t rv;
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_needed();

    SS_ASSERT(NULL != thread);
    rv = thread->id;

    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief Reset the thread helper
 */
void reset_thread_helper(void)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    if(true == thread_helper_data.initialized)
    {
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry); i++)
        {
            if(true == thread_helper_data.registry[i].in_use)
            {
                thread_helper_destroy_index(i);
            }
        }
    }
    thread_helper_data.initialized = false;
    init_if_needed();
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * Function to call when program exits to clean up all the hanging thread helper data
 */
void thread_helper_cleanup(void)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    reset_thread_helper();
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

