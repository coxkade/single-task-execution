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

#ifndef MAX_TASK_DATA_BUFFER_SIZE
#define MAX_TASK_DATA_BUFFER_SIZE 250
#endif //MAX_TASK_DATA_BUFFER_SIZE

struct simply_thread_task_s
{
    simply_thread_sem_t sem; //!< The Tasks signal semaphore
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
        uint8_t data_buffer[MAX_TASK_DATA_BUFFER_SIZE]; //!< Array that holds the raw data for the task
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

struct simply_thread_master_mutex_history_element_s
{
    const char *file;  //!< The File utilizing the mutex
    const char *function;  //!< The  function utilizing the mutex
    unsigned int line; //!< The line number utilizing the mutex
}; //!< Structure that holds info on the master mutexes history

struct simply_thread_master_mutex_fifo_entry_s
{
    pthread_t thread; //!< ID of the waiting thread
    uint64_t id; //!< The process id of the thread
    simply_thread_sem_t * sem; //!< The synchronization semaphore
    bool in_use; //!< Tells if the semaphore is in use
}; //!< Structure for the master mutex fifo entry

struct simply_thread_lib_data_s
{
    pthread_mutex_t init_mutex; //!< The modules initialization mutex
    simply_thread_sem_t master_semaphore; //!< The modules master semaphore
    struct
    {
        struct simply_thread_master_mutex_history_element_s current; //!< Where the master was obtained from
        struct simply_thread_master_mutex_history_element_s release; //!< Where the master was released from
        struct
        {
            struct simply_thread_master_mutex_fifo_entry_s entries[SIMPLY_THREAD_MAX_TASKS]; //!< The fifo entries
            unsigned int count; //!< The current count.  0 if semaphore is available
        } fifo; //!< Data for semaphore mutex scheduling
    } master_sem_data; //!< Data for helping debug the internal state
    struct simply_thread_task_s tcb_list[SIMPLY_THREAD_MAX_TASKS]; //!< Array of all the task control blocks
    struct simply_thread_sleep_data_s sleep; //!< Data for the sleep logic
    struct simply_thread_scheduler_task_data_s sched; //!< Data used by the scheduler task
    bool signals_initialized; //!< Tells if the signals have been initialized
    pthread_mutex_t print_mutex; //!< Mutex used by the log printer
    bool cleaning_up; //!< Boolean value that indicates if the system is currently in the progress of cleaning up.
};

#endif /* SIMPLY_THREAD_OBJECTS_H_ */
