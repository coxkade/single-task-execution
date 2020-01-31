/**
 * @file simply-thread-objects.h
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 * @brief Objects used by the simply thread library
 */

#include <pthread.h>
#include <stdbool.h>
#include <simply-thread.h>
#include <simply-thread-linked-list.h>
#include <pthread.h>
#include <simply-thread-sem-helper.h>

#ifndef SIMPLY_THREAD_OBJECTS_H_
#define SIMPLY_THREAD_OBJECTS_H_

#ifndef SIMPLY_THREAD_MAX_TASKS
#define SIMPLY_THREAD_MAX_TASKS 250
#endif //SIMPLY_THREAD_MAX_TASKS

enum simply_thread_thread_state_e
{
    SIMPLY_THREAD_TASK_RUNNING = 0,
    SIMPLY_THREAD_TASK_READY,
    SIMPLY_THREAD_TASK_BLOCKED,
    SIMPLY_THREAD_TASK_SUSPENDED,
    SIMPLY_THREAD_TASK_UNKNOWN_STATE,
    SIMPLY_THREAD__TASK_STATE_COUNT
}; //!< Enum for the different task states

struct simply_thread_task_s
{
    pthread_t thread; //!< The handle to the pthread
    enum simply_thread_thread_state_e state; //!< the current state of the thread
    unsigned int priority; //!< The Priority of the thread
    bool abort; //!< boolean that tells a thread to exit
    bool started; //!< Tells us the task has started at least once
    simply_thread_task_fnct fnct; //!< The task function
    const char *name;  //!< The name of the task
    struct
    {
        void *data; //!< The task data
        unsigned int data_size; //!< The Size of the task data
    } task_data; //!< Data to use with the task
}; //!< Structure that holds the data for a single task.

typedef struct simply_thread_scheduler_data_s
{
    struct simply_thread_task_s *task_adjust; //!< If not NULL pointer to the task with a state to update to
    enum simply_thread_thread_state_e new_state; //!<The New sate to set the task adjust to if it is valid
    bool sleeprequired; //!< Tells if the scheduler needs to force all the tasks to sleep
} simply_thread_scheduler_data_t; //!< Structure holding data for the scheduler to use

struct simply_thread_condition_s
{
    simply_thread_sem_t sig_sem; //!< The signaling semaphore
};


struct simply_thread_sleeper_data_s
{
    struct simply_thread_task_s *task_adjust; //!< The sleeping task
    unsigned int ms; //!< How long the task should sleep
    unsigned int current_ms; //!< How long the task has slept
}; //!< Structure used by the sleeper task to keep track of how long a task has been asleep

struct simply_thread_sleep_data_s
{
    struct
    {
        bool in_use; //!< Tells if the entry is in use
        struct simply_thread_sleeper_data_s sleep_data; //!The sleep data to use
    } sleep_list[SIMPLY_THREAD_MAX_TASKS];
}; //!< Structure for holding all libraries sleep data


struct simply_thread_scheduler_task_data_s
{
    struct simply_thread_condition_s condition; //!< Structure with elements to trigger the scheduler
    struct simply_thread_condition_s sleepcondition; //!< Structure for the sleep condition
    struct
    {
        struct simply_thread_scheduler_data_s work_data; //!< The data to work off of
        bool staged; //!< tells if the data is staged and waiting for action
        bool kill; //!< Tells the scheduler thread to close
    } sched_data; //!< Structure that holds the data the scheduler works off of
    bool threadlaunched; //!< Variable that tells if the thread has been launched
    pthread_t thread; //!< The thread ID of the scheduler thread
};

struct simply_thread_lib_data_s
{
    pthread_mutex_t master_mutex; //!< The modules master mutex
    simply_thread_linked_list_t thread_list; //!< The thread list handle
    struct simply_thread_sleep_data_s sleep; //!< Data for the sleep logic
    struct simply_thread_scheduler_task_data_s sched; //!< Data used by the scheduler task
    bool signals_initialized; //!< Tells if the signals have been initialized
    pthread_mutex_t print_mutex; //!< Mutex used by the log printer
    bool cleaning_up; //!< Boolean value that indicates if the system is currently in the progress of cleaning up.
};

#endif /* SIMPLY_THREAD_OBJECTS_H_ */
