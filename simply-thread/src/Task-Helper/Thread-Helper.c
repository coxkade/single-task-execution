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

#define DEBUG_THREAD_HELPER
#define PRINT_MSG(...) simply_thread_log(COLOR_MAGENTA, __VA_ARGS__)
#define PRINT_COLOR_MSG(C, ...) simply_thread_log(C, __VA_ARGS__)
#ifdef DEBUG_THREAD_HELPER
#else
#define PRINT_MSG(...)
#define PRINT_COLOR_MSG(C, ...)
#endif //DEBUG_THREAD_HELPER

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct thread_helper_module_data_s
{
    bool signals_initialized;
    helper_thread_t *current_thread;
    struct sigaction pause_action;
    struct sigaction kill_action;
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct thread_helper_module_data_s thread_helper_data =
{
    .signals_initialized = false,
    .current_thread = NULL
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
    helper_thread_t *worker = thread_helper_data.current_thread;
    int result = -1;
    if(NULL == worker)
    {
    	return;
    }
    SS_ASSERT(PAUSE_SIGNAL == signo);
    SS_ASSERT(NULL != worker);
    worker->thread_running = false;
    PRINT_MSG("\tThread No Longer Running\r\n");
//    while(0 != simply_thread_sem_wait(&worker->wait_sem)) {}
    while(0 != result)
    {
    	result = simply_thread_sem_wait(&worker->wait_sem);
    	if(0 != result)
    	{
    		PRINT_COLOR_MSG(COLOR_PURPLE, "---m_catch_pause returned %i\r\n", result);
    	}
    }
    worker->thread_running = true;
    PRINT_MSG("\tThread Now Running\r\n");
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
//    thread_helper_data.pause_action.sa_handler = SIG_IGN;
//    SS_ASSERT(0 == sigaction(PAUSE_SIGNAL, &thread_helper_data.pause_action, NULL));
}

/**
 * @brief Function that catches the kill message
 * @param signo
 */
static void m_catch_kill(int signo)
{
	PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(KILL_SIGNAL == signo);
    pthread_exit(NULL);
}

/**
 * @brief Function that initializes the module if required
 */
static void init_if_required(void)
{
    if(false == thread_helper_data.signals_initialized)
    {
    	PRINT_MSG("%s Initializing the module\r\n", __FUNCTION__);
    	thread_helper_data.pause_action.sa_handler = m_catch_pause;
//    	thread_helper_data.pause_action.sa_sigaction = &m_catch_pause;
//    	thread_helper_data.pause_action.sa_flags = SA_RESTART;
    	thread_helper_data.kill_action.sa_handler = m_catch_kill;
    	SS_ASSERT(0 == sigaction(PAUSE_SIGNAL, &thread_helper_data.pause_action, NULL));
    	SS_ASSERT(0 == sigaction(KILL_SIGNAL, &thread_helper_data.kill_action, NULL));
        atexit(sem_helper_cleanup);
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
    simply_thread_sem_init(&rv->wait_sem);
    assert(EAGAIN == simply_thread_sem_trywait(&rv->wait_sem));
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
    SS_ASSERT(0 == pthread_kill(thread->id, KILL_SIGNAL));
    pthread_join(thread->id, NULL);
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
static void thread_helper_pause_from_tcb(void * data)
{
	helper_thread_t *thread;
	thread = data;
	PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != thread);
    SS_ASSERT(thread->id != pthread_self());
    SS_ASSERT(NULL == thread_helper_data.current_thread);
    thread_helper_data.current_thread = thread;
    PRINT_MSG("\tTriggering the PAUSE_SIGNAL\r\n");
//    thread_helper_data.pause_action.sa_handler = m_catch_pause;
//	SS_ASSERT(0 == sigaction(PAUSE_SIGNAL, &thread_helper_data.pause_action, NULL));
    SS_ASSERT(0 == pthread_kill(thread->id, PAUSE_SIGNAL));
    while(true == thread->thread_running) {}
    SS_ASSERT(false == thread->thread_running);
    PRINT_MSG("\tSetting current thread to NULL\r\n");
    thread_helper_data.current_thread = NULL;
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Pause a thread
 * @param thread
 */
void thread_helper_pause_thread(helper_thread_t *thread)
{
	PRINT_MSG("%s Started\r\n", __FUNCTION__);
	init_if_required();
	run_in_tcb_context(thread_helper_pause_from_tcb, thread);
}


/**
 * @brief unpause a task from the task control context
 * @param data
 */
static void thread_helper_run_from_tcb(void * data)
{
	helper_thread_t *thread;
	thread = data;
	PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != thread);
    SS_ASSERT(thread->id != pthread_self());
    SS_ASSERT(NULL == thread_helper_data.current_thread);
    SS_ASSERT(false == thread->thread_running);
    SS_ASSERT(0 == simply_thread_sem_post(&thread->wait_sem));
    while(false == thread->thread_running) {}
    SS_ASSERT(true == thread->thread_running);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
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
	SS_ASSERT(NULL != thread);
	return thread->id;
}
