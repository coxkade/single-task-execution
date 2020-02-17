/**
 * @file simply-thread-mutex.c
 * @author Kade Cox
 * @date Created: Jan 21, 2020
 * @details
 * module that handles mutexes for the simply thread library.
 */


#include <simply-thread-mutex.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <simply-thread.h>
#include <simply-thread-linked-list.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/


#ifndef SIMPLY_THREAD_MUTEX_DEBUG
#ifdef DEBUG_SIMPLY_THREAD
#define SIMPLY_THREAD_MUTEX_DEBUG
#endif //DEBUG_SIMPLY_THREAD
#endif //SIMPLY_THREAD_MUTEX_DEBUG

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifdef SIMPLY_THREAD_MUTEX_DEBUG
#define PRINT_MSG(...) simply_thread_log(COLOR_LIGHT_GREEN, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //SIMPLY_THREAD_MUTEX_DEBUG

//Mutex to make pointer casting cleaner
#define HANDLE_DATA(X) ((struct mutex_data_s *)X)

//The maximum number of supported mutex
#ifndef SIMPLE_THREAD_MAX_MUTEX
#define SIMPLE_THREAD_MAX_MUTEX (250)
#endif //SIMPLE_THREAD_MAX_MUTEX

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/


struct mutex_local_block_data_element_s
{
    struct simply_thread_task_s *task;  //!<Pointer to the blocked task
}; //!< Structure for use with struct mutex_data_s->block_list

struct mutex_data_s
{
    const char *name;  //!< The Name of this mutex
    bool available; //!< Tells if the mutex is available
    struct mutex_local_block_data_element_s block_list[SIMPLE_THREAD_MAX_MUTEX]; //!< List of tasks waiting on this mutex
};//!< Structure for holding data unique to a mutex

struct mutex_wrapper_s
{
    struct mutex_data_s *ptr_mtx;  //!< Pointer to the mutex
};//!< Convenience structure for use with the linked lists

struct mutex_block_list_service_element_s
{
    struct
    {
        struct simply_thread_task_s *task;  //!< The blocked task
        struct mutex_data_s *mutex;  //!< The blocking mutex
        unsigned int max_count; //!< The max count
        unsigned int current_count; //!< The current count
        bool result; //!< The result of the mutex wait
    } task_data; //!< Structure containing the information that the timeout handler needs
    bool in_use; //!< Tells if the element is in use
};

struct mutex_module_data_s
{
    struct simply_thread_lib_data_s *lib_data;  //!< Pointer to the root library data
    simply_thread_linked_list_t mutex_list; //!< List of known mutexes
    struct mutex_block_list_service_element_s block_list[SIMPLE_THREAD_MAX_MUTEX]; //!< List of blocked tasks that need to be serviced
};//!< Structure for holding the data private to this module



/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/**
 * @brief Function that makes sure a mutexes block list is sound
 * @param mux pointer to the mutex in question
 */
static void simply_thread_check_mutex_block_list(struct mutex_data_s *mux);

/**
 * @brief cleanup and sort the block list for a mutex
 * @param mux pointer to the mutex data
 */
static void simply_thread_mutex_list_cleanup(struct mutex_data_s *mux);

/**
 * @brief get the number of tasks currently waiting on the mutex
 * @param mux pointer to the mutex data
 * @return
 */
static unsigned int mutex_get_wait_task_count(struct mutex_data_s *mux);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct mutex_module_data_s m_mutex_module_data =
{
    .lib_data = NULL,
    .mutex_list = NULL,
}; //!< Variable that holds the modules private data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief Function that destroys a single mutex
 * @param ptr_mtx pointer to the mutex to destroy and free
 */
static inline void destroy_mutex(struct mutex_data_s *ptr_mtx)
{
    assert(NULL != ptr_mtx);
    free(ptr_mtx);
}

/**
 * @brief Function that clears out the mutex list
 */
static inline void clear_mutex_list(void)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    struct mutex_wrapper_s *wrap_pointer;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    for(unsigned int i = 0; i < simply_thread_ll_count(m_mutex_module_data.mutex_list); i++)
    {
        wrap_pointer = simply_thread_ll_get(m_mutex_module_data.mutex_list, i);
        destroy_mutex(wrap_pointer->ptr_mtx);
    }
    simply_thread_ll_destroy(m_mutex_module_data.mutex_list);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_mutex_module_data.block_list); i++)
    {
        m_mutex_module_data.block_list[i].in_use = false;
    }

}

/**
 * @brief Function that initializes the simply thread mutex module
 */
void simply_thread_mutex_init(void)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    if(NULL == m_mutex_module_data.lib_data)
    {
        m_mutex_module_data.lib_data = simply_thread_lib_data();
    }
    assert(NULL != m_mutex_module_data.lib_data);
    m_mutex_module_data.mutex_list = simply_thread_new_ll(sizeof(struct mutex_wrapper_s));
    assert(NULL != m_mutex_module_data.mutex_list);

    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_mutex_module_data.block_list); i++)
    {
        m_mutex_module_data.block_list[i].in_use = false;
    }
}

/**
 * @brief Function that cleans up the simply thread mutexes
 */
void simply_thread_mutex_cleanup(void)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    if(NULL != m_mutex_module_data.mutex_list)
    {
        clear_mutex_list();
        m_mutex_module_data.mutex_list = NULL;
    }
}

/**
 * @brief Function that creates a mutex
 * @param name The name of the mutex
 * @return NULL on error.  Otherwise the mutex handle
 */
simply_thread_mutex_t simply_thread_mutex_create(const char *name)
{
    struct mutex_wrapper_s wrapper;
    PRINT_MSG("%s\r\n", __FUNCTION__);
    if(NULL == name)
    {
        return NULL;
    }

    wrapper.ptr_mtx = malloc(sizeof(struct mutex_data_s));
    assert(NULL != wrapper.ptr_mtx);
    wrapper.ptr_mtx->available = true;
    wrapper.ptr_mtx->name = name;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(wrapper.ptr_mtx->block_list); i++)
    {
        wrapper.ptr_mtx->block_list[i].task = NULL;
    }
    MUTEX_GET();
    assert(true == simply_thread_ll_append(m_mutex_module_data.mutex_list, &wrapper));
    MUTEX_RELEASE();
    return wrapper.ptr_mtx;
}


/**
 * @brief function that tells the highest priority blocked task that it can run
 * @param mux pointer to the blocking mutex
 */
static void simply_thread_mutex_unblock_next_task(struct mutex_data_s *mux)
{
    struct simply_thread_task_s *run_task = NULL;
    PRINT_MSG("%s\r\n", __FUNCTION__);
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    unsigned int waiting_tasks;
    simply_thread_check_mutex_block_list(mux);
    waiting_tasks = mutex_get_wait_task_count(mux);
    run_task = mux->block_list[0].task;
    if(NULL != run_task)
    {
        mux->block_list[0].task = NULL;
        simply_thread_mutex_list_cleanup(mux);
        assert(mutex_get_wait_task_count(mux) == (waiting_tasks - 1));
        if(SIMPLY_THREAD_TASK_BLOCKED == run_task->state)
        {
            PRINT_MSG("\tSet task %s to ready\r\n", run_task->name);
            simply_thread_set_task_state_from_locked(run_task, SIMPLY_THREAD_TASK_READY);
            assert(true == simply_thread_master_mutex_locked()); //We must be locked
        }
    }
    else
    {
        //There are no tasks waiting on the mutex.
        HANDLE_DATA(mux)->available = true;
        simply_ex_sched_from_locked();
    }
}

/**
 * @brief Function that unlocks a mutex
 * @param mux the mutex handle in question
 * @return true on success
 */
bool simply_thread_mutex_unlock(simply_thread_mutex_t mux)
{
    struct simply_thread_task_s *c_task;
    PRINT_MSG("%s\r\n", __FUNCTION__);
    if(NULL == mux)
    {
        return false;
    }
    MUTEX_GET();
    c_task = simply_thread_get_ex_task();
    if(false == HANDLE_DATA(mux)->available)
    {
        simply_thread_mutex_unblock_next_task(HANDLE_DATA(mux));
    }
    if(NULL == c_task)
    {
        PRINT_MSG("\t%s Mutex Unlocked\r\n", HANDLE_DATA(mux)->name);
    }
    else
    {
        PRINT_MSG("\t%s Mutex Unlocked: %s\r\n", HANDLE_DATA(mux)->name, c_task->name);
    }
    MUTEX_RELEASE();
    return true;
}

/**
 * @brief Function that makes sure a mutexes block list is sound
 * @param mux pointer to the mutex in question
 */
static void simply_thread_check_mutex_block_list(struct mutex_data_s *mux)
{
    assert(NULL != mux);
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    unsigned int i = 0;
    unsigned int last_priority = 0xFFFFFFFF;
    bool null_hit = false;
    do
    {
        if(true == null_hit)
        {
            assert(NULL == mux->block_list[i].task);
        }
        else
        {
            if(NULL == mux->block_list[i].task)
            {
                null_hit = true;
            }
            else
            {
                assert(last_priority >= mux->block_list[i].task->priority);
                last_priority = mux->block_list[i].task->priority;
            }
        }
        i++;
    }
    while(i < ARRAY_MAX_COUNT(mux->block_list));
}

/**
 * @brief Swap two list values
 * @param one pointer to the first swap element.
 * @param two pointer to the second swap element.
 */
static void simply_thread_element_swap(struct mutex_local_block_data_element_s *one, struct mutex_local_block_data_element_s *two)
{
    struct mutex_local_block_data_element_s worker;
    worker.task = one->task;
    one->task = two->task;
    two->task = worker.task;
}

/**
 * @brief cleanup and sort the block list for a mutex
 * @param mux pointer to the mutex data
 */
static void simply_thread_mutex_list_cleanup(struct mutex_data_s *mux)
{
    bool shifted;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    //send NULLS to the Back
    do
    {
        shifted = false;
        for(unsigned int i = 0; i < (ARRAY_MAX_COUNT(mux->block_list) - 1); i++)
        {
            if(NULL == mux->block_list[i].task && NULL != mux->block_list[i + 1].task)
            {
                simply_thread_element_swap(&mux->block_list[i], &mux->block_list[i + 1]);
                shifted = true;
            }
        }
    }
    while(true == shifted);
    //Send the Highest priority task to the front
    do
    {
        shifted = false;
        for(unsigned int i = 0; i < (ARRAY_MAX_COUNT(mux->block_list) - 1); i++)
        {
            if(NULL != mux->block_list[i].task && NULL != mux->block_list[i + 1].task)
            {
                if(mux->block_list[i].task->priority < mux->block_list[i + 1].task->priority)
                {
                    simply_thread_element_swap(&mux->block_list[i], &mux->block_list[i + 1]);
                    shifted = true;
                }
            }
        }
    }
    while(true == shifted);
    simply_thread_check_mutex_block_list(mux);
}

/**
 * @brief get the number of tasks currently waiting on the mutex
 * @param mux
 * @return
 */
static unsigned int mutex_get_wait_task_count(struct mutex_data_s *mux)
{
    unsigned int rv = 0;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(mux->block_list); i++)
    {
        if(NULL != mux->block_list[i].task)
        {
            rv++;
        }
    }
    return rv;
}
/**
 * Function that adds a task to the task blocked list
 * @param mux
 * @param task
 */
static void simply_thread_add_task_blocked(struct mutex_data_s *mux, struct simply_thread_task_s *task)
{
    bool added;
    unsigned int task_count;
    struct simply_thread_task_s *c_task;
    assert(NULL != task);
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    task_count = mutex_get_wait_task_count(mux);
    c_task = simply_thread_get_ex_task();
    assert(c_task == task);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(mux->block_list); i++)
    {
        assert(mux->block_list[i].task != task);
    }
    added = false;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(mux->block_list) && false == added; i++)
    {
        if(NULL == mux->block_list[i].task)
        {
            mux->block_list[i].task = task;
            added = true;
        }
    }
    assert((task_count + 1) == mutex_get_wait_task_count(mux));
    simply_thread_mutex_list_cleanup(mux);
    assert((task_count + 1) == mutex_get_wait_task_count(mux));
}

/**
 * @brief Wait for a mutex to be available
 * @param mux
 * @param wait_time
 * @param task
 * @return
 */
static bool simply_thread_mutex_lockdown(struct mutex_data_s *mux, unsigned int wait_time, struct simply_thread_task_s *task)
{
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    assert(NULL != task);
    bool value_found = false;
    bool rv = false;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_mutex_module_data.block_list) && false == value_found; i++)
    {
        if(false == m_mutex_module_data.block_list[i].in_use)
        {
            value_found = true;
            m_mutex_module_data.block_list[i].task_data.current_count = 0;
            m_mutex_module_data.block_list[i].task_data.mutex = HANDLE_DATA(mux);
            m_mutex_module_data.block_list[i].task_data.result = true;
            m_mutex_module_data.block_list[i].task_data.task = task;
            m_mutex_module_data.block_list[i].task_data.max_count = wait_time;
            m_mutex_module_data.block_list[i].in_use = true;
            assert(NULL != m_mutex_module_data.block_list[i].task_data.task);
            simply_thread_add_task_blocked(mux, task);
            PRINT_MSG("\tSet task %s to blocked\r\n", task->name);
            simply_thread_set_task_state_from_locked(task, SIMPLY_THREAD_TASK_BLOCKED);
            assert(true == simply_thread_master_mutex_locked()); //We must be locked
            rv = m_mutex_module_data.block_list[i].task_data.result;
            m_mutex_module_data.block_list[i].in_use = false;
        }
    }
    return rv;
}

/**
 * @brief Function that locks a mutex
 * @param mux handle of the mutex to lock
 * @param wait_time How long to wait to obtain a lock
 * @return true on success
 */
bool simply_thread_mutex_lock(simply_thread_mutex_t mux, unsigned int wait_time)
{
    struct simply_thread_task_s *c_task;
    bool rv;

    PRINT_MSG("%s\r\n", __FUNCTION__);

    if(NULL == mux)
    {
        return false;
    }
    rv = true;
    MUTEX_GET();
    c_task = simply_thread_get_ex_task();
    if(false == HANDLE_DATA(mux)->available)
    {
        //The mutex is not available
        if(NULL == c_task)
        {
            //We cannot wait in the interrupt context
            rv = false;
        }
        else
        {
            rv = simply_thread_mutex_lockdown(HANDLE_DATA(mux), wait_time, c_task);
        }
    }
    else
    {
        //Calling mutex gets the mutex
        HANDLE_DATA(mux)->available = false;
    }
    if(true == rv)
    {
        if(NULL == c_task)
        {
            PRINT_MSG("\t%s Mutex Locked\r\n", HANDLE_DATA(mux)->name);
        }
        else
        {
            PRINT_MSG("\t%s Mutex Locked: %s\r\n", HANDLE_DATA(mux)->name, c_task->name);
        }
    }
    MUTEX_RELEASE();
    return rv;
}

/**
 * @brief function the systic needs to call
 */
void simply_thread_mutex_maint(void)
{
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_mutex_module_data.block_list);  i++)
    {
        if(true == m_mutex_module_data.block_list[i].in_use)
        {
            m_mutex_module_data.block_list[i].task_data.current_count++;
        }
    }
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_mutex_module_data.block_list);  i++)
    {
        if(true == m_mutex_module_data.block_list[i].in_use)
        {
            assert(NULL != m_mutex_module_data.block_list[i].task_data.task);
            if(m_mutex_module_data.block_list[i].task_data.max_count <= m_mutex_module_data.block_list[i].task_data.current_count)
            {
                if(SIMPLY_THREAD_TASK_BLOCKED == m_mutex_module_data.block_list[i].task_data.task->state)
                {
                    PRINT_MSG("%s Mutex Timed out\r\n", m_mutex_module_data.block_list[i].task_data.mutex->name);
                    m_mutex_module_data.block_list[i].task_data.result = false;
                    simply_thread_set_task_state_from_locked(m_mutex_module_data.block_list[i].task_data.task, SIMPLY_THREAD_TASK_READY);
                    assert(true == simply_thread_master_mutex_locked()); //We must be locked
                }
            }
        }
    }
}
