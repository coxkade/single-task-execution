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

//#define DEBUG_SIMPLY_THREAD

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
    pthread_t sched_id; //!< The Id of the current scheduler task
    bool clear_in_progress;
    bool Sched_Waiting; //!< Flag that says the schedualler is waiting to exicute
    pthread_mutex_t clear_mutex; //!< Mutex that provides mutual exclusion for the clear function
    struct {
    	sem_helper_sem_t tcb_mutex; //!< Mutex that protects the TCB context
    	bool initialized; //!< Tells if this component has been initialized
    } mutex; //!< Structure that holds the data needed to protect the TCB context
}; //!< Structure for the local module data

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct tcb_module_data_s tcb_module_data =
{
    .sched_id = NULL,
    .clear_in_progress = false,
    .Sched_Waiting = false,
    .clear_mutex = PTHREAD_MUTEX_INITIALIZER,
	.mutex = {
			.initialized = false,
	}
}; //!< The modules local data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief worker function that initializes the mutex
 * @param unused
 */
static void * init_tcb_mux(void * unused)
{
	SS_ASSERT(0 == pthread_mutex_lock(&tcb_module_data.clear_mutex));
	if(false == tcb_module_data.mutex.initialized)
	{
		Sem_Helper_sem_init(&tcb_module_data.mutex.tcb_mutex);

		SS_ASSERT(0 == Sem_Helper_sem_post(&tcb_module_data.mutex.tcb_mutex));

		tcb_module_data.mutex.initialized = true;
	}
	pthread_mutex_unlock(&tcb_module_data.clear_mutex);
	return NULL;
}

/**
 * @brief Function that
 */
static void init_tcm_mux_if_needed(void)
{
	if(false == tcb_module_data.mutex.initialized)
	{
		pthread_t id;
		SS_ASSERT(0 == pthread_create(&id, NULL, init_tcb_mux, NULL));
		pthread_join(id, NULL);
	}
}

/**
 * Internal function for waiting on a semaphore
 * @param sem
 */
static void _int_sem_lock(sem_helper_sem_t * sem)
{
	int result = -1;
    while(0 != result)
    {
        result = Sem_Helper_sem_wait(sem);
    }
}

/**
 * @brief Function for obtaining the tcb context mutex
 */
static void tcb_mutex_obtain(void)
{
	init_tcm_mux_if_needed();
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	_int_sem_lock(&tcb_module_data.mutex.tcb_mutex);
}

/**
 * @brief Function for releasing the tcb context mutex
 */
static void tcb_mutex_release(void)
{
	init_tcm_mux_if_needed();


	SS_ASSERT(0 == Sem_Helper_sem_post(&tcb_module_data.mutex.tcb_mutex));
	while(true == tcb_module_data.Sched_Waiting) {}

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
 * @brief function to call when the application exits
 */
static void tcb_on_exit(void)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    if(true == tcb_module_data.clear_in_progress)
    {
        void *callstack[256];
        int i, frames = backtrace(callstack, 256);
        char **strs = backtrace_symbols(callstack, frames);
        for(i = 0; i < frames; ++i)
        {
            PRINT_MSG("%s\n", strs[i]);
        }
        free(strs);
    }
    while(true == tcb_module_data.clear_in_progress) {}

    thread_helper_cleanup();
    Sem_Helper_clean_up();
    ss_log_on_exit();
}

/**
 * Function that clears out the scheduler of all tasks
 * @param unused
 */
static void *tcb_sched_clear(void *unused)
{

    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    SS_ASSERT(0 == pthread_mutex_lock(&tcb_module_data.clear_mutex));
    tcb_module_data.clear_in_progress = true;
    thread_helper_cleanup();
    if(NULL != tcb_module_data.sched_id)
    {
    	PRINT_MSG("\tKilling the scheduler task\r\n");
    	pthread_kill(tcb_module_data.sched_id, KILL_SIGNAL);
    	pthread_join(tcb_module_data.sched_id, NULL);
    	tcb_module_data.sched_id = NULL;
    }
    Sem_Helper_clean_up();
    tcb_module_data.mutex.initialized = false;
    tcb_module_data.clear_in_progress = false;
    tcb_module_data.Sched_Waiting = false;
    pthread_mutex_unlock(&tcb_module_data.clear_mutex);
    PRINT_MSG("Finishing %s\r\n", __FUNCTION__);
    return NULL;
}

//!< Clears all the data in the tcb
static void tcb_clear(void)
{
    int result;
    pthread_t killer_thread;

	result = pthread_create(&killer_thread, NULL, tcb_sched_clear, NULL);
	if(0 != result)
	{
		ST_LOG_ERROR("%s failed to launch kill thread\r\n");
		assert(true == false);
	}
	//Wait for the kill thread to complete
	pthread_join(killer_thread, NULL);
}

/**
 * @brief Function that runs the scheduler
 * @param unused
 */
static void * tcb_sched_exe(void * unused)
{
	tcb_task_t * starttask;
	tcb_mutex_obtain();
	PRINT_MSG("%s Starting\r\n", __FUNCTION__);
	//First lets pause all running threads
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks); i++)
	{
		if(true == tcb_module_data.tasks[i].in_use)
		{
			if(SIMPLY_THREAD_TASK_RUNNING == tcb_module_data.tasks[i].task.state)
			{
				if(true == thread_helper_thread_running(tcb_module_data.tasks[i].task.thread))
				{
					PRINT_MSG("Stopping task %s\r\n", tcb_module_data.tasks[i].task.name);
					thread_helper_pause_thread(tcb_module_data.tasks[i].task.thread);
				}
				tcb_module_data.tasks[i].task.state = SIMPLY_THREAD_TASK_READY;
			}
		}
	}

	starttask = NULL;
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks); i++)
	{
		if(true == tcb_module_data.tasks[i].in_use)
		{
			if(SIMPLY_THREAD_TASK_READY == tcb_module_data.tasks[i].task.state)
			{
				if(NULL == starttask)
				{
					starttask = &tcb_module_data.tasks[i].task;
				}
				else if(starttask->priority < tcb_module_data.tasks[i].task.priority)
				{
					starttask = &tcb_module_data.tasks[i].task;
				}
			}
		}
	}

	if(NULL != starttask)
	{
		PRINT_MSG("Starting task %s\r\n", starttask->name);
		starttask->state = SIMPLY_THREAD_TASK_RUNNING;
		thread_helper_run_thread(starttask->thread);
	}

	tcb_module_data.Sched_Waiting = false;
	tcb_module_data.sched_id = NULL;
	tcb_mutex_release();
	PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
	return NULL;
}

/**
 * Function that launches the tcb scheduler
 */
static void tcb_launch_scheduler(void)
{
	PRINT_MSG("%s Starting\r\n", __FUNCTION__);
	if(false == tcb_module_data.Sched_Waiting)
	{
		tcb_module_data.Sched_Waiting = true;
		SS_ASSERT(0 == pthread_create(&tcb_module_data.sched_id, NULL, tcb_sched_exe, NULL));
	}
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * reset the task control block data
 */
void tcb_reset(void)
{
	static bool first_time = true;
	struct tcb_entry_s *c_task;

	PRINT_MSG("%s Starting\r\n", __FUNCTION__);

	tcb_mutex_obtain();
	if(true == first_time)
	{
		PRINT_MSG("\tSetting up on exit\r\n");
		atexit(tcb_on_exit);
		first_time = false;
	}

	tcb_clear(); //This will wipe out our mutex so we need to obtain it again after
	tcb_mutex_obtain();
	//Initialize the module data
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks); i++)
	{
		c_task = &tcb_module_data.tasks[i];
		c_task->in_use = false;
	}
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	tcb_mutex_release();
}

/**
 * @brief Function changes a tasks state
 * @param state
 * @param task
 */
void tcb_set_task_state(enum simply_thread_thread_state_e state, tcb_task_t *task)
{
	tcb_mutex_obtain();
	PRINT_MSG("%s Starting\r\n", __FUNCTION__);
	SS_ASSERT(NULL != task);
	SS_ASSERT(SIMPLY_THREAD_TASK_RUNNING != state);

	if(task->state != state)
	{
		task->state = state;
		PRINT_MSG("State set to %i\r\n", state);
		tcb_launch_scheduler();
	}
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	tcb_mutex_release();
}

/**
 * @brief Fetch the current state of a task
 * @param task
 * @return the current state of the task
 */
enum simply_thread_thread_state_e tcb_get_task_state(tcb_task_t *task)
{
	enum simply_thread_thread_state_e rv;

	tcb_mutex_obtain();
	PRINT_MSG("%s Starting\r\n", __FUNCTION__);
	rv = task->state;
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	tcb_mutex_release();
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
	tcb_task_t * rv;
	struct tcb_entry_s * c_task;

	tcb_mutex_obtain();
	PRINT_MSG("%s Starting\r\n", __FUNCTION__);

	rv = NULL;
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks) && NULL == rv; i++)
	{
		c_task = &tcb_module_data.tasks[i];
		if(false == c_task->in_use)
		{
			c_task->in_use = true;
			rv = &c_task->task;
		}
	}

	if(NULL != rv)
	{
		rv->cb = cb;
		rv->continue_on_run = false;
		rv->data = data;
		rv->data_size = data_size;
		rv->name = name;
		rv->priority = priority;
		rv->state = SIMPLY_THREAD_TASK_READY;
		rv->thread = thread_helper_thread_create(task_runner_function, rv);
		SS_ASSERT(NULL != rv->thread);
		//Pause the newly created task
		thread_helper_pause_thread(rv->thread);
		//set continue_on_run so that the thread can resume when unpaused
		rv->continue_on_run = true;
		tcb_launch_scheduler();
	}
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	tcb_mutex_release();
	return rv;
}

/**
 * @brief Fetch the task of the calling task
 * @return NULL if the task is not in the TCB
 */
tcb_task_t *tcb_task_self(void)
{
	tcb_task_t * rv;
	helper_thread_t * me;

	tcb_mutex_obtain();
	PRINT_MSG("%s Starting\r\n", __FUNCTION__);
	rv = NULL;
	me = thread_helper_self();
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(tcb_module_data.tasks) && NULL == rv; i++)
	{
		if(true == tcb_module_data.tasks[i].in_use)
		{
			if(me == tcb_module_data.tasks[i].task.thread)
			{
				rv = &tcb_module_data.tasks[i].task;
			}
		}
	}

	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	tcb_mutex_release();
	return rv;
}

/**
 * @brief Blocking function that runs a function in the task control block context.
 * @param fnct The function to run
 * @param data the data for the function
 */
void run_in_tcb_context(void (*fnct)(void *), void *data)
{
	SS_ASSERT(NULL != fnct);

	tcb_mutex_obtain();
	PRINT_MSG("%s Starting\r\n", __FUNCTION__);
	fnct(data);
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	tcb_mutex_release();
}

/**
 * Function to call when an assert occurs
 */
void tcb_on_assert(void)
{
	tcb_clear();
}

