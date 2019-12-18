/**
 * @file simply-thread-scheduler.c
 * @author Kade Cox
 * @date Created: Dec 17, 2019
 * @details
 * 
 */

#include <simply-thread-scheduler.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_CYAN, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/




struct module_data_s
{
	struct simply_thread_condition_s condition; //!< Structure with elements to trigger the scheduler
	struct simply_thread_condition_s sleepcondition; //!< Structure for the sleep condition
	struct
	{
		pthread_mutex_t mutex; //!< mutex to protect the data
		struct simply_thread_scheduler_data_s work_data; //!< The data to work off of
		bool staged; //!< tells if the data is staged and waiting for action
		bool kill; //!< Tells the scheduler thread to close
	}sched_data;//!< Structure that holds the data the scheduler works off of
	struct simply_thread_task_list_s * threadlist; //!< Pointer to the system thread list
	bool threadlaunched; //!< Variable that tells if the thread has been launcged
	pthread_t thread; //!< The thread ID of the scheduler thread
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/


/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct module_data_s m_module_data={
	.threadlaunched = false
}; //!< The module data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief function that fetches the number of running tasks
 * @return the number of running tasks
 */
static inline unsigned int m_running_tasks(void)
{
    struct simply_thread_task_s *c;
    unsigned int rv = 0;
    for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.threadlist->handle); i++)
    {
        c = (struct simply_thread_task_s *)simply_thread_ll_get(m_module_data.threadlist->handle, i);
        if(SIMPLY_THREAD_TASK_RUNNING == c->state)
        {
            rv++;
        }
    }
    assert(2 > rv);
    return rv;
}

/**
 * @brief Function that sleeps all running tasks
 */
static inline void m_sleep_all_tasks(void)
{
    struct simply_thread_task_s *c;

    while(0 != m_running_tasks())
    {
        for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.threadlist->handle); i++)
        {
            c = (struct simply_thread_task_s *)simply_thread_ll_get(m_module_data.threadlist->handle, i);
            assert(NULL != c);
            if(SIMPLY_THREAD_TASK_RUNNING == c->state)
            {
                assert(0 == pthread_kill(c->thread, SIGUSR1));
                pthread_mutex_unlock(&m_module_data.threadlist->mutex);
                simply_thread_wait_condition(&m_module_data.sleepcondition);
                assert(0 == pthread_mutex_lock(&m_module_data.threadlist->mutex));
            }
        }
    }

}

/**
 * @brief tell the best task to start
 */
static inline void m_sched_run_best_task(void)
{
    struct simply_thread_task_s *c;
    struct simply_thread_task_s *best_task;
    best_task = NULL;
    for(unsigned int i = 0; i < simply_thread_ll_count(m_module_data.threadlist->handle); i++)
    {
        c = (struct simply_thread_task_s *)simply_thread_ll_get(m_module_data.threadlist->handle, i);
        assert(NULL != c);
        if(SIMPLY_THREAD_TASK_READY == c->state)
        {
            if(NULL == best_task)
            {
                best_task = c;
            }
            else if(best_task->priority < c->priority)
            {
                best_task = c;
            }
        }
    }
    if(NULL != best_task)
    {
    	PRINT_MSG("\tTask %s resume\r\n", best_task->name);
        best_task->state = SIMPLY_THREAD_TASK_RUNNING;
    }
}

static void *m_run_sched(void *data)
{
	PRINT_MSG("%s Launched\r\n", __FUNCTION__);
    assert(NULL == data);
    while(1)
    {
        PRINT_MSG("\tWaiting on condition\r\n");
    	simply_thread_wait_condition(&m_module_data.condition);
        PRINT_MSG("\tCondition met\r\n");
    	assert(0 == pthread_mutex_lock(&m_module_data.sched_data.mutex));
    	assert(true == m_module_data.sched_data.staged);
    	if(true == m_module_data.sched_data.kill)
    	{
    		pthread_mutex_unlock(&m_module_data.sched_data.mutex);
    		return NULL;
    	}
    	assert(0 == pthread_mutex_lock(&m_module_data.threadlist->mutex));
    	if(true == m_module_data.sched_data.work_data.sleeprequired)
    	{
    		PRINT_MSG("\tScheduler sleeping all tasks\r\n");
    		m_sleep_all_tasks();
    	}
    	else
    	{
            while(0 != m_running_tasks()){};
            assert(0 == m_running_tasks());
    	}
    	if(NULL != m_module_data.sched_data.work_data.task_adjust)
        {
            //All tasks are asleep set the new state
        	PRINT_MSG("\tScheduler updating task state\r\n");
        	m_module_data.sched_data.work_data.task_adjust->state =m_module_data.sched_data.work_data.new_state;
        }
    	PRINT_MSG("\tScheduler Launching Next Task\r\n");
    	m_sched_run_best_task();

    	pthread_mutex_unlock(&m_module_data.threadlist->mutex);
    	m_module_data.sched_data.staged = false;
    	pthread_mutex_unlock(&m_module_data.sched_data.mutex);
    }
    return NULL;
}

/**
 * @brief initialize a mutex
 * @param mutex pointer to the mutex to initialize
 */
static void m_mutex_init(pthread_mutex_t * mutex)
{
	pthread_mutexattr_t attr;
	assert(0 == pthread_mutexattr_init(&attr));
	assert(0 == pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
	assert(0 == pthread_mutex_init(mutex, &attr));
}

/**
 * @brief initialize the module
 * @param thread_list pointer to the thread list
 */
void simply_thread_scheduler_init(struct simply_thread_task_list_s * thread_list)
{
	PRINT_MSG("%s\r\n", __FUNCTION__);
	assert(NULL != thread_list);
	m_mutex_init(&m_module_data.sched_data.mutex);
	simply_thread_init_condition(&m_module_data.condition);
	simply_thread_init_condition(&m_module_data.sleepcondition);
	assert(0 == pthread_mutex_lock(&m_module_data.sched_data.mutex));
	m_module_data.sched_data.staged = false;
	m_module_data.sched_data.kill = false;
	m_module_data.threadlist = thread_list;
	//Launch the scheduler thread
	assert(0 == pthread_create(&m_module_data.thread, NULL, m_run_sched, NULL));
	m_module_data.threadlaunched = true;
	pthread_mutex_unlock(&m_module_data.sched_data.mutex);
}

/**
 * @brief Kill the scheduler and cleanup the module
 */
void simply_thread_scheduler_kill(void)
{
	PRINT_MSG("%s\r\n", __FUNCTION__);
	bool sched_running = true;
	if(true == m_module_data.threadlaunched)
	{
		assert(0 == pthread_mutex_lock(&m_module_data.sched_data.mutex));
		do
		{
			sched_running = m_module_data.sched_data.staged;
			if(true == sched_running)
			{
				pthread_mutex_unlock(&m_module_data.sched_data.mutex);
				simply_thread_sleep_ns(500);
				assert(0 == pthread_mutex_lock(&m_module_data.sched_data.mutex));
			}
		}while(true == sched_running);
		m_module_data.sched_data.work_data.new_state = SIMPLY_THREAD_TASK_UNKNOWN_STATE;
		m_module_data.sched_data.work_data.task_adjust = NULL;
		m_module_data.sched_data.staged = true;
		m_module_data.sched_data.kill = true;
		simply_thread_send_condition(&m_module_data.condition);
		pthread_mutex_unlock(&m_module_data.sched_data.mutex);
		pthread_join(m_module_data.thread, NULL);
		assert(0 == pthread_mutex_destroy(&m_module_data.sched_data.mutex));
		simply_thread_dest_condition(&m_module_data.condition);
		simply_thread_dest_condition(&m_module_data.sleepcondition);
		m_module_data.threadlaunched = false;
	}
}

/**
 * @brief tell the scheduler to run
 * @param thread_data Data for the scheduler to use
 */
void simply_thread_run(struct simply_thread_scheduler_data_s * thread_data)
{
	PRINT_MSG("%s\r\n", __FUNCTION__);
	bool sched_running = true;
	assert(0 == pthread_mutex_lock(&m_module_data.sched_data.mutex));
	do
	{
		sched_running = m_module_data.sched_data.staged;
		if(true == sched_running)
		{
			pthread_mutex_unlock(&m_module_data.sched_data.mutex);
			simply_thread_sleep_ns(500);
			assert(0 == pthread_mutex_lock(&m_module_data.sched_data.mutex));
		}
	}while(true == sched_running);

	//Ok stage the data to go out
	m_module_data.sched_data.work_data.new_state = thread_data->new_state;
	m_module_data.sched_data.work_data.task_adjust = thread_data->task_adjust;
	m_module_data.sched_data.work_data.sleeprequired = thread_data->sleeprequired;
	m_module_data.sched_data.staged = true;
	simply_thread_send_condition(&m_module_data.condition);
	pthread_mutex_unlock(&m_module_data.sched_data.mutex);
}

/**
 * @brief tell the scheduler that a task has gone to sleep
 * @param ptr_task
 */
void simply_thread_tell_sched_task_sleeping(struct simply_thread_task_s *ptr_task)
{
	PRINT_MSG("%s\r\n", __FUNCTION__);
	simply_thread_send_condition(&m_module_data.sleepcondition);
}
