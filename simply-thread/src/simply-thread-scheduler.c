/**
 * @file simply-thread-scheduler.c
 * @author Kade Cox
 * @date Created: Dec 17, 2019
 * @details
 *
 */

#define _GNU_SOURCE
#include <simply-thread-scheduler.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <fcntl.h>

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

#define MODULE_DATA simply_thread_lib_data()->sched

#define TASK_LIST simply_thread_lib_data()->thread_list


/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/


/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/


/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/


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

    for(unsigned int i = 0; i < simply_thread_ll_count(TASK_LIST); i++)
    {
        c = (struct simply_thread_task_s *)simply_thread_ll_get(TASK_LIST, i);
        if(SIMPLY_THREAD_TASK_RUNNING == c->state)
        {
            rv++;
        }
    }
    assert(2 > rv);
    return rv;
}

/**
 * @brief Function that exits the scheduler thread if needed
 */
static inline void m_sched_exit_if_kill(void)
{
    if(true == MODULE_DATA.sched_data.kill)
    {
        MODULE_DATA.sched_data.staged = false;
        MODULE_DATA.threadlaunched = false;
        MUTEX_RELEASE();
        pthread_exit(NULL);
    }
}

/**
 * @brief Function that sleeps all running tasks
 */
static inline void m_sleep_all_tasks(void)
{
    struct simply_thread_task_s *c;

    while(0 != m_running_tasks())
    {
        for(unsigned int i = 0; i < simply_thread_ll_count(TASK_LIST); i++)
        {
            c = (struct simply_thread_task_s *)simply_thread_ll_get(TASK_LIST, i);
            assert(NULL != c);
            if(SIMPLY_THREAD_TASK_RUNNING == c->state)
            {
                m_sched_exit_if_kill();
                assert(0 == pthread_kill(c->thread, SIGUSR1));
                MUTEX_RELEASE();
                simply_thread_wait_condition(&MODULE_DATA.sleepcondition);
                MUTEX_GET();
                m_sched_exit_if_kill();
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
    for(unsigned int i = 0; i < simply_thread_ll_count(TASK_LIST); i++)
    {
        c = (struct simply_thread_task_s *)simply_thread_ll_get(TASK_LIST, i);
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
        assert(0 == simply_thread_sem_post(&best_task->sem));
    }
}

/**
 * @brief the worker task for the scheduler
 * @param data
 */
static void *m_run_sched(void *data)
{
    PRINT_MSG("%s Launched\r\n", __FUNCTION__);
    assert(NULL == data);
    while(1)
    {
        PRINT_MSG("\tWaiting on condition\r\n");
        simply_thread_wait_condition(&MODULE_DATA.condition);
        PRINT_MSG("\tCondition met\r\n");
        MUTEX_GET();
        assert(true == MODULE_DATA.sched_data.staged);
        m_sched_exit_if_kill();

        if(true == MODULE_DATA.sched_data.work_data.sleeprequired)
        {
            PRINT_MSG("\tScheduler sleeping all tasks\r\n");
            m_sleep_all_tasks();
        }
        else
        {
            bool wait = true;
            do
            {
                if(0 == m_running_tasks())
                {
                    wait = false;
                }
                else
                {
                    m_sched_exit_if_kill();
                    MUTEX_RELEASE();
                    simply_thread_sleep_ns(137);
                    MUTEX_GET();
                }
            }
            while(true == wait);
            assert(0 == m_running_tasks());
        }
        if(NULL != MODULE_DATA.sched_data.work_data.task_adjust)
        {
            //All tasks are asleep set the new state
            PRINT_MSG("\tScheduler updating task state\r\n");
            MODULE_DATA.sched_data.work_data.task_adjust->state = MODULE_DATA.sched_data.work_data.new_state;
        }
        PRINT_MSG("\tScheduler Launching Next Task\r\n");
        m_sched_run_best_task();
        MODULE_DATA.sched_data.staged = false;
        MUTEX_RELEASE();
    }
    return NULL;
}


/**
 * @brief initialize the module
 */
void simply_thread_scheduler_init(void)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    simply_thread_init_condition(&MODULE_DATA.condition);
    simply_thread_init_condition(&MODULE_DATA.sleepcondition);
    PRINT_MSG("\tSched Condition: %p\r\n", &MODULE_DATA.condition);
    PRINT_MSG("\tSleep Condition: %p\r\n", &MODULE_DATA.sleepcondition);
    MODULE_DATA.sched_data.staged = false;
    MODULE_DATA.sched_data.kill = false;
    //Launch the scheduler thread
    assert(0 == pthread_create(&MODULE_DATA.thread, NULL, m_run_sched, NULL));
    MODULE_DATA.threadlaunched = true;
}

/**
 * @brief Kill the scheduler and cleanup the module
 */
void simply_thread_scheduler_kill(void)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    bool sched_running = true;
    MUTEX_GET();
    if(true == MODULE_DATA.threadlaunched)
    {
        do
        {
            MODULE_DATA.sched_data.kill = true;
            sched_running = MODULE_DATA.sched_data.staged;
            if(true == sched_running)
            {
                MUTEX_RELEASE();
                simply_thread_sleep_ns(127);
                MUTEX_GET();
            }
        }
        while(true == sched_running);
        MODULE_DATA.sched_data.work_data.new_state = SIMPLY_THREAD_TASK_UNKNOWN_STATE;
        MODULE_DATA.sched_data.work_data.task_adjust = NULL;
        MODULE_DATA.sched_data.staged = true;
        simply_thread_send_condition(&MODULE_DATA.condition);
        MUTEX_RELEASE();
        pthread_join(MODULE_DATA.thread, NULL);
        MUTEX_GET();
        simply_thread_dest_condition(&MODULE_DATA.condition);
        simply_thread_dest_condition(&MODULE_DATA.sleepcondition);
        MODULE_DATA.threadlaunched = false;
    }
    MUTEX_RELEASE();
}

/**
 * @brief tell the scheduler to run
 * @param thread_data Data for the scheduler to use
 */
void simply_thread_run(struct simply_thread_scheduler_data_s *thread_data)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    MUTEX_GET();
    if(false == MODULE_DATA.threadlaunched)
    {
        MUTEX_RELEASE();
        return;
    }
    bool sched_running = true;
    do
    {
        sched_running = MODULE_DATA.sched_data.staged;
        if(true == sched_running)
        {
            MUTEX_RELEASE();
            simply_thread_sleep_ns(131);
            MUTEX_GET();
            if(false == MODULE_DATA.threadlaunched)
            {
                MUTEX_RELEASE();
                return;
            }
        }
    }
    while(true == sched_running);

    //Ok stage the data to go out
    MODULE_DATA.sched_data.work_data.new_state = thread_data->new_state;
    MODULE_DATA.sched_data.work_data.task_adjust = thread_data->task_adjust;
    MODULE_DATA.sched_data.work_data.sleeprequired = thread_data->sleeprequired;
    MODULE_DATA.sched_data.staged = true;
    simply_thread_send_condition(&MODULE_DATA.condition);
    MUTEX_RELEASE();
}

/**
 * @brief tell the scheduler that a task has gone to sleep
 * @param ptr_task
 */
void simply_thread_tell_sched_task_sleeping(struct simply_thread_task_s *ptr_task)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    simply_thread_send_condition(&MODULE_DATA.sleepcondition);
}
