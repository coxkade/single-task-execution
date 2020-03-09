/**
 * @file Thread-Helper.c
 * @author Kade Cox
 * @date Created: Mar 9, 2020
 * @details
 *
 */

#include <Thread-Helper.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct thread_helper_module_data_s
{
    bool signals_initialized;
    helper_thread_t *current_thread;
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
	printf("%s Started\r\n", __FUNCTION__);
    helper_thread_t *worker = thread_helper_data.current_thread;
    printf("checking signal\r\n");
    assert(PAUSE_SIGNAL == signo);
    printf("checking worker\r\n");
    assert(NULL != worker);
    printf("setting thread running to false\r\n");
    worker->thread_running = false;
    printf("Calling sem wait\r\n");
    while(0 != simply_thread_sem_wait(&worker->wait_sem)) {}
    printf("Finished sem wait \r\n");
    worker->thread_running = true;
}

/**
 * @brief Function that catches the kill message
 * @param signo
 */
static void m_catch_kill(int signo)
{
	printf("%s Started\r\n", __FUNCTION__);
    assert(KILL_SIGNAL == signo);
    pthread_exit(NULL);
}

/**
 * @brief Function that initializes the module if required
 */
static void init_if_required(void)
{
    if(false == thread_helper_data.signals_initialized)
    {
    	assert(SIG_ERR != signal(PAUSE_SIGNAL, SIG_IGN));
        assert(SIG_ERR != signal(PAUSE_SIGNAL, m_catch_pause));
        assert(SIG_ERR != signal(KILL_SIGNAL, m_catch_kill));
        printf("Registered the Signals\r\n");
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
    helper_thread_t *typed;
    typed = data;
    assert(NULL != typed);
    assert(NULL != typed->worker);
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
    helper_thread_t *rv;
    assert(NULL != worker);
    init_if_required();
    rv = malloc(sizeof(helper_thread_t));
    assert(NULL != rv);
    rv->thread_running = false;
    rv->worker = worker;
    rv->worker_data = data;
    simply_thread_sem_init(&rv->wait_sem);
    assert(0 == simply_thread_sem_wait(&rv->wait_sem));
    assert(0 == pthread_create(&rv->id, NULL, thread_helper_worker, rv));
    while(false == rv->thread_running) {}
    assert(true == rv->thread_running);
    return rv;
}

/**
 * @brief Destroy a previously created thread.  This thread blocks
 * @param thread
 */
void thread_helper_thread_destroy(helper_thread_t *thread)
{
    init_if_required();
    assert(NULL != thread);
    assert(0 == pthread_kill(thread->id, KILL_SIGNAL));
    pthread_join(thread->id, NULL);
    simply_thread_sem_destroy(&thread->wait_sem);
    free(thread);
}

/**
 * @brief Check if a thread is running
 * @param thread The thread to check
 * @return True if the thread is running.  False otherwise.
 */
bool thread_helper_thread_running(helper_thread_t *thread)
{
    init_if_required();
    assert(NULL != thread);
    return thread->thread_running;
}

/**
 * @brief Pause a thread
 * @param thread
 */
void thread_helper_pause_thread(helper_thread_t *thread)
{
    init_if_required();
    assert(NULL != thread);
    assert(thread->id != pthread_self());
    assert(NULL == thread_helper_data.current_thread);
    thread_helper_data.current_thread = thread;
    assert(0 == pthread_kill(thread->id, PAUSE_SIGNAL));
    while(true == thread->thread_running) {}
    assert(false == thread->thread_running);
    thread_helper_data.current_thread = NULL;
}

/**
 * @brief Run a thread
 * @param thread
 */
void thread_helper_run_thread(helper_thread_t *thread)
{
    init_if_required();
    assert(NULL != thread);
    assert(thread->id != pthread_self());
    assert(NULL == thread_helper_data.current_thread);
    assert(false == thread->thread_running);
    thread_helper_data.current_thread = thread;
    assert(0 == simply_thread_sem_post(&thread->wait_sem));
    while(false == thread->thread_running) {}
    assert(true == thread->thread_running);
    thread_helper_data.current_thread = NULL;
}

/**
 * @brief get the id of the worker thread
 * @param thread
 * @return the thread id
 */
pthread_t thread_helper_get_id(helper_thread_t *thread)
{
	assert(NULL != thread);
	return thread->id;
}
