/**
 * @file master-mutex.c
 * @author Kade Cox
 * @date Created: Mar 2, 2020
 * @details
 *
 */

#include <simply-thread-sem-helper.h>
#include <simply-thread-log.h>
#include <master-mutex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifdef DEBUG_FIFO
#define PRINT_MSG(...) simply_thread_log(COLOR_SKY_BLUE, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_FIFO

#define SEM_WAIT(s) while(0 != simply_thread_sem_wait(s)){}

#define SEM_POST(s) simply_thread_sem_post(s)

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct master_mutex_data_s
{
    pthread_mutex_t init_mutex; //!< The mutex to protect the module initialization
    bool initialized;
    bool locked;
    bool alloc_allowed;
    simply_thread_sem_t sem;
    pthread_t locking_thread;
    simply_thread_sem_t localsem;
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct master_mutex_data_s m_master_mutex_data =
{
    .initialized = false,
    .init_mutex = PTHREAD_MUTEX_INITIALIZER,
    .alloc_allowed = false
};

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

static void initialize_if_required(void)
{
    if(false == m_master_mutex_data.initialized)
    {
        PRINT_MSG("Initializing the master mutex\r\n");
        assert(0 == pthread_mutex_lock(&m_master_mutex_data.init_mutex));
        if(false == m_master_mutex_data.initialized)
        {
            simply_thread_sem_init(&m_master_mutex_data.sem);
            simply_thread_sem_init(&m_master_mutex_data.localsem);
            m_master_mutex_data.locked = false;
            m_master_mutex_data.initialized = true;
            m_master_mutex_data.alloc_allowed = true;
        }
        pthread_mutex_unlock(&m_master_mutex_data.init_mutex);
    }
}

/**
 * @brief fetch the mutex
 * @return true on success
 */
bool master_mutex_get(void)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    initialize_if_required();
    SEM_WAIT(&m_master_mutex_data.sem);
    SEM_WAIT(&m_master_mutex_data.localsem);
    m_master_mutex_data.locked = true;
    m_master_mutex_data.locking_thread = pthread_self();
    SEM_POST(&m_master_mutex_data.localsem);
    while(false == m_master_mutex_data.alloc_allowed) {}
    return m_master_mutex_data.locked;
}

/**
 * @brief release the master mutex
 */
void master_mutex_release(void)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    initialize_if_required();
    SEM_WAIT(&m_master_mutex_data.localsem);
    m_master_mutex_data.locked = false;
    assert(m_master_mutex_data.locking_thread == m_master_mutex_data.locking_thread);
    SEM_POST(&m_master_mutex_data.sem);
    SEM_POST(&m_master_mutex_data.localsem);
}

/**
 * @brief Reset the master mutex module
 */
void master_mutex_reset(void)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    initialize_if_required();
    assert(true == master_mutex_locked());
    PRINT_MSG("\tThe mutex is locked\r\n");
    SEM_WAIT(&m_master_mutex_data.localsem);
    PRINT_MSG("Getting the init mutex\r\n");
    assert(0 == pthread_mutex_lock(&m_master_mutex_data.init_mutex));
    m_master_mutex_data.initialized = false;
    m_master_mutex_data.alloc_allowed = false;
    PRINT_MSG("\tDestroying m_master_mutex_data.localsem\r\n");
    simply_thread_sem_destroy(&m_master_mutex_data.localsem);
    SEM_POST(&m_master_mutex_data.sem);
    PRINT_MSG("\tDestroying m_master_mutex_data.sem\r\n");
    simply_thread_sem_destroy(&m_master_mutex_data.sem);
    m_master_mutex_data.locked = false;
    pthread_mutex_unlock(&m_master_mutex_data.init_mutex);
    PRINT_MSG("Finishing %s\r\n", __FUNCTION__);
}

/**
 * @brief tells if the master mutex is locked
 * @return true if the mutex is currently locked
 */
bool master_mutex_locked(void)
{
    bool rv = false;
    SEM_WAIT(&m_master_mutex_data.localsem);
    rv = m_master_mutex_data.locked;
    SEM_POST(&m_master_mutex_data.localsem);
    return rv;
}

/**
 * Pull the fifo entry off of the fifo queue for the current task
 * @return NULL if entry does not exist.
 */
master_mutex_entry_t master_mutex_pull(void)
{
    master_mutex_entry_t rv;
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    rv = (master_mutex_entry_t)1;
    return rv;
}

/**
 * @brief push a previously pulled entry back onto the fifo
 * @param entry
 */
void master_mutex_push(master_mutex_entry_t entry)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
}

/**
 * Function that makes the fifo mutex safe to be interupted
 */
void master_mutex_prep_signal(void)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
}

/**
 * Function That clears the prep flags
 */
void master_mutex_clear_prep_signal(void)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
}
