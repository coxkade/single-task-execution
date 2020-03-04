/**
 * @file fifo-mutex.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 * A single fifo mutex
 */

#include <simply-thread-log.h>
#include <simply-thread-sem-helper.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdarg.h>
#include "priv-inc/master-mutex.h"

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

//Macro that blocks on a sem_wait
#define SEM_WAIT(S) while(0 != simply_thread_sem_wait(S)){}

//Macro that unblocks on a sem release
#define SEM_POST(S) assert(0 == simply_thread_sem_post(S))

#define MASTER_WAIT \
    SEM_WAIT(&m_fifo_data.main_sem); \
    m_fifo_data.lock_line = __LINE__

#define MASTER_POST \
    m_fifo_data.lock_line = 0;\
    SEM_POST(&m_fifo_data.main_sem)

#ifndef MAX_LIST_SIZE
#define MAX_LIST_SIZE 500
#endif //MAX_LIST_SIZE

#ifdef DEBUG_MUTEX
#define PRINT_MSG(...) fifo_mutex_printf(__VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_MUTEX


/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct fifo_registry_s
{
    simply_thread_sem_t *sem;
    pthread_t id;
    bool enabled;
}; //!< Registry entry data

struct fifo_master_mutex_data_s
{
    simply_thread_sem_t main_sem; //!< The main semaphore to keep everything safe
    struct fifo_registry_s registry[MAX_LIST_SIZE]; //!< The registery data
    bool locked; //!< Tells if we are currently locked
    bool initialized; //!< Tells if the module has been initialized
    int wait_count; //!< The Number of waiting tasks
    pthread_mutex_t init_mutex; //!< The mutex to protect the module initialization
    unsigned int lock_line; //!< Tells what line locked the semaphore
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/


/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct fifo_master_mutex_data_s m_fifo_data =
{
    .initialized = false,
    .init_mutex = PTHREAD_MUTEX_INITIALIZER
};

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


#ifdef DEBUG_MUTEX
/**
 * @brief Internal print message that tags the thread printing the message
 * @param fmt
 */
static void fifo_mutex_printf(const char *fmt, ...)
{
    int rc;
    char buffer[500];
    va_list args;
    va_start(args, fmt);
    rc = vsnprintf(buffer, ARRAY_MAX_COUNT(buffer), fmt, args);
    assert(0 < rc);
    va_end(args);

    simply_thread_log(COLOR_ORANGE, "%p: %s", pthread_self(), buffer);
}
#endif //DEBUG_MUTEX

/**
 * @brief Function that initializes this module as needed
 */
static void fifo_mutex_init_if_needed(void)
{
    if(false == m_fifo_data.initialized)
    {
        assert(0 == pthread_mutex_lock(&m_fifo_data.init_mutex));
        if(false == m_fifo_data.initialized)
        {
            for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_fifo_data.registry); i++)
            {
                m_fifo_data.registry[i].enabled = false;
                m_fifo_data.registry[i].sem = NULL;
            }
            m_fifo_data.locked = false;
            simply_thread_sem_init(&m_fifo_data.main_sem);
            m_fifo_data.initialized = true;
            m_fifo_data.wait_count = 0;
            PRINT_MSG("************ %i wait_count set to %i\r\n", __LINE__, m_fifo_data.wait_count);
        }
        pthread_mutex_unlock(&m_fifo_data.init_mutex);
    }
}

/**
 * @brief Function that fetches a registry to wait on
 * @return NULL on error
 */
static struct fifo_registry_s *fetch_wait_entry(void)
{
    struct fifo_registry_s *rv;
    rv = NULL;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_fifo_data.registry) && NULL == rv; i++)
    {
        if(NULL == m_fifo_data.registry[i].sem)
        {
            rv = &m_fifo_data.registry[i];
        }
    }
    return rv;
}

/**
 * @brief Fetch next entry to release
 * @return NULL if nothing to release, otherwise pointer to the entry
 */
static struct fifo_registry_s *fetch_release_entry(void)
{
    struct fifo_registry_s *rv;
    rv = NULL;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_fifo_data.registry) && NULL == rv; i++)
    {
        if(NULL != m_fifo_data.registry[i].sem && true == m_fifo_data.registry[i].enabled)
        {
            rv = &m_fifo_data.registry[i];
        }
    }
    return rv;
}

static void fifo_remove_sem_from_reg(simply_thread_sem_t *sem)
{
    bool stop = false;
    unsigned int remove_count = 0;
    assert(NULL != sem);

    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_fifo_data.registry) && false == stop; i++)
    {
        if(sem == m_fifo_data.registry[i].sem)
        {
            m_fifo_data.registry[i].sem = NULL;
            m_fifo_data.registry[i].enabled = false;
            remove_count++;
        }
        if(NULL == m_fifo_data.registry[i].sem)
        {
            stop = true;
        }
    }
    assert(1 == remove_count);
}

/**
 * @brief function that cleans up the registry
 */
static void fifo_clean_registery(void)
{
    bool swap_happened = true;
    bool stop = false;
    while(true == swap_happened)
    {
        swap_happened = false;
        stop = false;
        for(unsigned int i = 1; i < ARRAY_MAX_COUNT(m_fifo_data.registry) && false == stop; i++)
        {
            if(NULL == m_fifo_data.registry[i - 1].sem && NULL != m_fifo_data.registry[i].sem)
            {
                //Need to swap the values
                memcpy(&m_fifo_data.registry[i - 1], &m_fifo_data.registry[i], sizeof(m_fifo_data.registry[i]));
                m_fifo_data.registry[i].sem = NULL;
                m_fifo_data.registry[i].enabled = false;
                m_fifo_data.registry[i].id = 0;
                swap_happened = true;
            }
            if(NULL == m_fifo_data.registry[i - 1].sem && NULL == m_fifo_data.registry[i].sem)
            {
                stop = true;
            }
        }
    }
}


/**
 * @brief fetch the mutex
 * @return true on success
 */
bool master_mutex_get(void)
{
    struct fifo_registry_s *wait_entry;
    simply_thread_sem_t *worker_sem;
    wait_entry = NULL;
    worker_sem = NULL;
    fifo_mutex_init_if_needed();
    MASTER_WAIT;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    if(false == m_fifo_data.locked && 0 == m_fifo_data.wait_count)
    {
        //We are not locked and do not need to wait
        PRINT_MSG("\tNot locked can proceed\r\n");
        m_fifo_data.locked = true;
        PRINT_MSG("\tSet Locked to TRUE %i\r\n", __LINE__);
        wait_entry = NULL;
    }
    else
    {
        //Se need to get a mutex to wait on
        PRINT_MSG("\tLocked Wait Required\r\n");
        wait_entry = fetch_wait_entry();
        assert(NULL != wait_entry);
        wait_entry->sem = malloc(sizeof(simply_thread_sem_t));
        assert(NULL != wait_entry->sem);
        simply_thread_sem_init(wait_entry->sem);
        PRINT_MSG("\tCreated new sem: %p\r\n", wait_entry->sem);
        wait_entry->enabled = true;
        wait_entry->id = pthread_self();
        assert(0 == simply_thread_sem_trywait(wait_entry->sem));
        worker_sem = wait_entry->sem;
        m_fifo_data.wait_count++;
        PRINT_MSG("************ %i wait_count set to %i\r\n", __LINE__, m_fifo_data.wait_count);
    }
    MASTER_POST;
    if(NULL != worker_sem)
    {
        PRINT_MSG("\tWaiting on %p\r\n", worker_sem);
        SEM_WAIT(worker_sem); //Wait for our semaphore
        assert(NULL != worker_sem);
        PRINT_MSG("\tFinished Waiting on %p\r\n", worker_sem);
        MASTER_WAIT;
        //Clean up our ticket
        m_fifo_data.wait_count--;
        PRINT_MSG("************ %i wait_count set to %i\r\n", __LINE__, m_fifo_data.wait_count);
        assert(0 <= m_fifo_data.wait_count);
        PRINT_MSG("\tDestroying wait semaphore\r\n");
        simply_thread_sem_destroy(worker_sem);
        PRINT_MSG("\tWait Semaphore destroyed\r\n");
        fifo_remove_sem_from_reg(worker_sem);
        free(worker_sem);
        fifo_clean_registery();
        PRINT_MSG("\tSet Locked to TRUE %i\r\n", __LINE__);
        m_fifo_data.locked = true;
        MASTER_POST;
    }
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return true;
}

/**
 * @brief release the master mutex
 */
void master_mutex_release(void)
{
    struct fifo_registry_s *next_entry;
    fifo_mutex_init_if_needed();
    MASTER_WAIT;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    next_entry = fetch_release_entry();
    if(NULL != next_entry)
    {
        assert(true == next_entry->enabled);
        assert(NULL != next_entry->sem);
        PRINT_MSG("\tTelling next task to start with %p\r\n", next_entry->sem);
        SEM_POST(next_entry->sem);
    }
    else
    {
        assert(0 == m_fifo_data.wait_count);
    }
    PRINT_MSG("\tSet Locked to FALSE %i\r\n", __LINE__);
    m_fifo_data.locked = false;
    MASTER_POST;
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Reset the master mutex module
 */
void master_mutex_reset(void)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    assert(0 == pthread_mutex_lock(&m_fifo_data.init_mutex));
    if(true == m_fifo_data.initialized)
    {
        PRINT_MSG("\tWe are initialized and need to clean stuff up\r\n");
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_fifo_data.registry); i++)
        {
            if(NULL != m_fifo_data.registry[i].sem)
            {
                simply_thread_sem_destroy(m_fifo_data.registry[i].sem);
            }
        }
        m_fifo_data.initialized = false;
        simply_thread_sem_destroy(&m_fifo_data.main_sem);
    }
    pthread_mutex_unlock(&m_fifo_data.init_mutex);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief tells if the master mutex is locked
 * @return true if the mutex is currently locked
 */
bool master_mutex_locked(void)
{
    fifo_mutex_init_if_needed();
    return m_fifo_data.locked;
}

/**
 * Pull the fifo entry off of the fifo queue for the current task
 * @return NULL if entry does not exist.
 */
master_mutex_entry_t master_mutex_pull(void)
{
    pthread_t id;
    master_mutex_entry_t rv;
    struct fifo_registry_s *worker;
    rv = NULL;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    MASTER_WAIT;
    id = pthread_self();
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_fifo_data.registry) && NULL == rv; i++)
    {
        if(NULL != m_fifo_data.registry[i].sem && id == m_fifo_data.registry[i].id)
        {
            worker = malloc(sizeof(struct fifo_registry_s));
            assert(NULL != worker);
            rv = worker;
            m_fifo_data.registry[i].enabled = false;
            memcpy(worker, &m_fifo_data.registry[i], sizeof(m_fifo_data.registry[i]));
        }
    }
    if(NULL != rv)
    {
        assert(0 < m_fifo_data.wait_count);
        m_fifo_data.wait_count--;
        PRINT_MSG("************ %i wait_count set to %i\r\n", __LINE__, m_fifo_data.wait_count);
    }
    MASTER_POST;
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief push a previously pulled entry back onto the fifo
 * @param entry
 */
void master_mutex_push(master_mutex_entry_t entry)
{
    struct fifo_registry_s *typed;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    MASTER_WAIT;
    typed = entry;
    assert(NULL != typed);
    assert(NULL != typed->sem);
    assert(false == typed->enabled);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_fifo_data.registry) && false == typed->enabled; i++)
    {
        if(typed->id == m_fifo_data.registry[i].id && typed->sem == m_fifo_data.registry[i].sem)
        {
            assert(false == m_fifo_data.registry[i].enabled);
            typed->enabled = true;
            m_fifo_data.registry[i].enabled = true;
        }
    }
    m_fifo_data.wait_count++;
    PRINT_MSG("************ %i wait_count set to %i\r\n", __LINE__, m_fifo_data.wait_count);
    MASTER_POST;
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    free(typed);
}

/**
 * Function that makes the fifo mutex safe to be interupted
 */
void master_mutex_prep_signal(void)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    MASTER_WAIT;
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * Function That clears the prep flags
 */
void master_mutex_clear_prep_signal(void)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    MASTER_POST;
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}


