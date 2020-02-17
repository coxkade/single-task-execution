/**
 * @file fifo-mutex.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 * A single fifo mutex
 */

#include <fifo-mutex.h>
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

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/


//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

//Macro that blocks on a sem_wait
#define SEM_BLOCK(S) while(0 != simply_thread_sem_wait(&S)){}

//Macro that unblocks on a sem release
#define SEM_UNBLOCK(S) assert(0 == simply_thread_sem_post(&S))

//Macro to shorten global variable
#define M_DATA fifo_module_data

#ifndef MAX_LIST_SIZE
#define MAX_LIST_SIZE 250
#endif //MAX_LIST_SIZE

#ifdef DEBUG_FIFO
#define PRINT_MSG(...) simply_thread_log(COLOR_SKY_BLUE, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_FIFO

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct ticket_entry_s
{
    simply_thread_sem_t *ticket;
    pthread_t id;
};

struct fifo_mutex_module_data_s
{
    bool initialized;
    pthread_mutex_t init_mutex;
    simply_thread_sem_t ticket_semaphore;
    struct ticket_entry_s ticket_table[MAX_LIST_SIZE];
    bool started;
    bool locked;  //!< Tells if we are currently locked
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/**
 * @brief Function that initializes the master semaphore
 */
static void m_init_master_semaphore(void);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct fifo_mutex_module_data_s fifo_module_data =
{
    .init_mutex = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false,
};

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief Function that sleeps for the specified number of nanoseconds
 * @param ns number of nanoseconds to sleep
 */
static void fifo_mutex_sleep_ns(unsigned long ns)
{
    struct timespec time_data =
    {
        .tv_sec = 0,
        .tv_nsec = ns
    };

    while(0 != nanosleep(&time_data, &time_data))
    {
    }
}

/**
 * @brief get the ticket table entry for the process by id
 * @param id the process id
 * @return NULL on error otherwise the entry
 */
static struct ticket_entry_s *process_entry(pthread_t id)
{
    unsigned int entry_count;
    struct ticket_entry_s *rv;
    rv = NULL;
    entry_count = 0;
    for(int i = 0; i < ARRAY_MAX_COUNT(M_DATA.ticket_table); i++)
    {
        if(M_DATA.ticket_table[i].ticket != NULL && M_DATA.ticket_table[i].id == id)
        {
            entry_count++;
        }
    }
    assert(2 > entry_count);
    for(int i = 0; i < ARRAY_MAX_COUNT(M_DATA.ticket_table) && NULL == rv; i++)
    {
        if(M_DATA.ticket_table[i].ticket != NULL && M_DATA.ticket_table[i].id == id)
        {
            rv = &M_DATA.ticket_table[i];
        }
    }
    return rv;
}

/**
 * @brief Fetch the next available ticket table entry
 * @return NULL on error, otherwise a pointer to the entry
 */
static struct ticket_entry_s *next_available_entry(void)
{
    struct ticket_entry_s *rv;
    rv = NULL;
    for(int i = 0; i < ARRAY_MAX_COUNT(M_DATA.ticket_table) && NULL == rv; i++)
    {
        if(M_DATA.ticket_table[i].ticket == NULL)
        {
            rv = &M_DATA.ticket_table[i];
        }
    }
    return rv;
}

/**
 * @brief function that checks if a ticket is in use
 * @param ticket the ticket to check
 * @return The number of times the ticket is in the queue
 */
static int ticket_use_count(simply_thread_sem_t *ticket)
{
    int rv;
    rv = 0;
    assert(NULL != ticket);
    for(int i = 0; i < ARRAY_MAX_COUNT(M_DATA.ticket_table); i++)
    {
        if(ticket == M_DATA.ticket_table[i].ticket)
        {
            rv++;
        }
    }
    return rv;
}

/**
 * @brief Function that creates a new ticket
 * @return Pointer to new ticket. asserts on error
 */
simply_thread_sem_t *new_ticket(void)
{
    simply_thread_sem_t *rv;
    rv = malloc(sizeof(simply_thread_sem_t));
    assert(NULL != rv);
    simply_thread_sem_init(rv);
    assert(0 == simply_thread_sem_trywait(rv));
    return rv;
}

/**
 * @brief fetch an execution ticket
 * @return The ticket dat you need
 */
static struct ticket_entry_s fifo_mutex_fetch_ticket(void)
{
    struct ticket_entry_s *existing;
    struct ticket_entry_s *next;
    struct ticket_entry_s entry;
    pthread_t id;
    SEM_BLOCK(fifo_module_data.ticket_semaphore);
    id = pthread_self();
    existing = process_entry(id);
    next = next_available_entry();
    assert(NULL != next);

    if(NULL != existing)
    {
        //We already have a semaphore for the task
        assert(existing->id == id);
        next->id = existing->id;
        next->ticket = existing->ticket;
    }
    else
    {
        next->id = id;
        next->ticket = new_ticket();
    }
    if(&fifo_module_data.ticket_table[0] == next && false == fifo_module_data.locked)
    {
        //We are the first entry on the table and are already locked
        PRINT_MSG("\tadded first entry\r\n");
        SEM_UNBLOCK(next->ticket[0]);
    }
    else
    {
        PRINT_MSG("\tadded non first entry\r\n");
    }
    entry.id = id;
    entry.ticket = next->ticket;
    SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    return entry;
}

/**
 * @brief Shift the entire ticket counter
 */
static void ticket_counter_shift(void)
{
    for(int i = 1; i < ARRAY_MAX_COUNT(M_DATA.ticket_table); i++)
    {
        M_DATA.ticket_table[i - 1].id = M_DATA.ticket_table[i].id;
        M_DATA.ticket_table[i - 1].ticket = M_DATA.ticket_table[i].ticket;
    }
}

/**
 * @brief wait for our entries turn for the mutex
 * @param entry pointer to our entry
 */
static void fifo_mutex_wait_turn(struct ticket_entry_s *entry)
{
    assert(NULL != entry);
    assert(NULL != entry->ticket);
    PRINT_MSG("\tWaiting on ticket %p\r\n", entry->ticket);
    SEM_BLOCK((entry->ticket[0]));
    PRINT_MSG("\tGot ticket %p\r\n", entry->ticket);
    SEM_BLOCK(fifo_module_data.ticket_semaphore);
    assert(entry->ticket == fifo_module_data.ticket_table[0].ticket);
    if(1 == ticket_use_count(entry->ticket))
    {
        //We need to free the memory
        simply_thread_sem_destroy(entry->ticket);
        free(entry->ticket);
    }
    fifo_module_data.ticket_table[0].ticket = NULL;
    ticket_counter_shift();
    PRINT_MSG("Finished Waiting\r\n");
    fifo_module_data.locked = true;
    SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
}

/**
 * @brief fetch the mutex
 * @return true on success
 */
bool fifo_mutex_get(void)
{
    struct ticket_entry_s entry;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    m_init_master_semaphore();
    entry = fifo_mutex_fetch_ticket();
    fifo_mutex_wait_turn(&entry);
    return true;
}

/**
 * @brief tells if the fifo mutex is locked
 * @return true if the mutex is currently locked
 */
bool fifo_mutex_locked(void)
{
    bool rv = true;
    m_init_master_semaphore();
    SEM_BLOCK(fifo_module_data.ticket_semaphore);
    rv = fifo_module_data.locked;
    SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    assert(true == rv);
    return rv;
}

/**
 * @brief function that verifies a new task started running
 * @return true if a new task started running
 */
static bool fifo_task_resumed(void)
{
    static const unsigned int ns_period = 1000000 / 2;
    static const unsigned int timeout_count = 100 * 2;
    unsigned int current_count = 0;
    while(false == fifo_module_data.started)
    {
        fifo_mutex_sleep_ns(ns_period);
        current_count++;
        if(current_count >= timeout_count)
        {
        	ST_LOG_ERROR("******* Task Resume Timed Out *********\r\n");
            return false;
        }
    }
    return true;
}

/**
 * @brief release the fifo mutex
 */
void fifo_mutex_release(void)
{
    bool first_time = true;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    m_init_master_semaphore();
    do
    {
        SEM_BLOCK(M_DATA.ticket_semaphore);
        if(true == first_time)
        {
            fifo_module_data.locked = false;
            first_time = false;
        }
        //We need to shift the ticket counter
        if(NULL != M_DATA.ticket_table[0].ticket)
        {
            fifo_module_data.started = false;
            PRINT_MSG("\tSelecting Ticket %p\r\n", M_DATA.ticket_table[0].ticket);
            SEM_UNBLOCK(M_DATA.ticket_table[0].ticket[0]);
        }
        else
        {
            fifo_module_data.started = true;
        }
        SEM_UNBLOCK(M_DATA.ticket_semaphore);
    }
    while(false == fifo_task_resumed());
}

/**
 * @brief Reset the fifo mutex module
 */
void fifo_mutex_reset(void)
{
    assert(0 == pthread_mutex_lock(&fifo_module_data.init_mutex));
    if(true == fifo_module_data.initialized)
    {
        simply_thread_sem_destroy(&fifo_module_data.ticket_semaphore);
        fifo_module_data.initialized = false;
    }
    pthread_mutex_unlock(&fifo_module_data.init_mutex);
}

/**
 * @brief Function that initializes the master semaphore
 */
static void m_init_master_semaphore(void)
{
    if(false == fifo_module_data.initialized)
    {
        assert(0 == pthread_mutex_lock(&fifo_module_data.init_mutex));
        if(false == fifo_module_data.initialized)
        {
            simply_thread_sem_init(&fifo_module_data.ticket_semaphore);

            for(unsigned int i = 0; i < ARRAY_MAX_COUNT(fifo_module_data.ticket_table); i++)
            {
                fifo_module_data.ticket_table[i].ticket = NULL;
            }
            fifo_module_data.started = false;
            fifo_module_data.initialized = true;
            fifo_module_data.locked = false;
        }
        pthread_mutex_unlock(&fifo_module_data.init_mutex);
    }
}
