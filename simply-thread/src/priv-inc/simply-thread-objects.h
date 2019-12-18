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

#ifndef SIMPLY_THREAD_OBJECTS_H_
#define SIMPLY_THREAD_OBJECTS_H_

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

struct simply_thread_task_list_s
{
	pthread_mutex_t mutex; //!< Mutex to protect the list
	simply_thread_linked_list_t handle; //!< The handle to the list
}; //!< Structure for holding the task list

struct simply_thread_condition_s
{
	pthread_mutex_t gate_mutex; //!< mutex for race condition
	bool waiting;//!< Value that says if we are waiting
};

#endif /* SIMPLY_THREAD_OBJECTS_H_ */
