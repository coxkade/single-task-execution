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

#define DEBUG_FIFO

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
//    bool started;
    simply_thread_sem_t * sync_semaphore;
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
	.sync_semaphore = NULL
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
//    fifo_module_data.started = true;

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
    if(NULL != fifo_module_data.sync_semaphore)
    {
    	SEM_UNBLOCK(fifo_module_data.sync_semaphore[0]);
    	fifo_module_data.sync_semaphore = NULL;
    }
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
 * @brief Function that calculates the number of milliseconds elapsed between to times
 * @param x pointer to the earlier time
 * @param y pointer to the later time
 * @return the time value used
 */
static inline unsigned int time_diff(struct timeval *x, struct timeval *y)
{
	struct time_diff_value_s
	{
		uint64_t s;
		uint64_t us;
	};

	static const unsigned int ms_per_s = 1000;
	static const unsigned int us_per_ms = 1000;
	static const unsigned int us_per_s = 1000000;
	struct time_diff_value_s x_worker;
	struct time_diff_value_s y_worker;
	struct time_diff_value_s diff;
	unsigned int rv = 0;

	assert(NULL != x && NULL != y);
	x_worker.us = (uint64_t) x->tv_usec;
	x_worker.s = (uint64_t) x->tv_sec;
	y_worker.us = (uint64_t) y->tv_usec;
	y_worker.s = (uint64_t) y->tv_sec;

	//Sanity checks
	assert(x_worker.s <= y_worker.s);
	if(x_worker.s == y_worker.s)
	{
		assert(x_worker.us <= y_worker.us);
	}

	if(x_worker.us > y_worker.us)
	{
		assert(x_worker.s < y_worker.s);
		y_worker.us = y_worker.us + us_per_s;
		y_worker.s = y_worker.s - 1;
	}

	assert(x_worker.s <= y_worker.s);
	assert(x_worker.us <= y_worker.us);

	//Calculate the difference
	diff.s = y_worker.s - x_worker.s;
	diff.us = y_worker.us - x_worker.us;

	rv = (diff.us/us_per_ms) + (diff.s * ms_per_s);

	return rv;
}

/**
 * @brief function that verifies a new task started running
 * @return true if a new task started running
 */
static bool fifo_task_resumed(simply_thread_sem_t * sync)
{
	struct timeval start;
	struct timeval current;
	unsigned int ms_elapsed =0;
	static const unsigned int ns_period = 100;
	static const unsigned int max_wait_ms = 100; //Max number of ms to wait for start
	bool started;
	simply_thread_sem_t * sync_sem;
	sync_sem = sync;
	assert(0 == ms_elapsed);
	gettimeofday(&start, NULL);
	started = false;
	if(NULL == sync_sem)
	{
		started = true;
	}
	while(false == started)
	{
		gettimeofday(&current, NULL);
		ms_elapsed = time_diff(&start, &current);
		if(max_wait_ms < ms_elapsed)
		{

			ST_LOG_ERROR("******* Task Resume Timed Out %u*********\r\n", ms_elapsed);
			fifo_mutex_sleep_ns(ns_period);
			return false;
		}

		if(0 == simply_thread_sem_trywait(sync_sem))
		{
			SEM_BLOCK(M_DATA.ticket_semaphore);
			started = true;
			simply_thread_sem_destroy(sync_sem);
			free(sync_sem);
			assert(NULL != M_DATA.sync_semaphore);
			SEM_UNBLOCK(M_DATA.ticket_semaphore);
		}

	}
	return true;
}

/**
 * @brief release the fifo mutex
 */
void fifo_mutex_release(void)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    m_init_master_semaphore();
    simply_thread_sem_t * sync;
	SEM_BLOCK(M_DATA.ticket_semaphore);
	PRINT_MSG("\t%s has ticket semaphore\r\n");

	assert(NULL == fifo_module_data.sync_semaphore);
	fifo_module_data.locked = false;

	//We need to shift the ticket counter
	if(NULL != M_DATA.ticket_table[0].ticket)
	{
		fifo_module_data.sync_semaphore = malloc(sizeof(simply_thread_sem_t));
		assert(NULL != fifo_module_data.sync_semaphore);
		PRINT_MSG("\tSelecting Ticket %p\r\n", M_DATA.ticket_table[0].ticket);
		SEM_UNBLOCK(M_DATA.ticket_table[0].ticket[0]);
	}
	else
	{
		fifo_module_data.sync_semaphore = NULL;
	}
	sync = fifo_module_data.sync_semaphore;
	SEM_UNBLOCK(M_DATA.ticket_semaphore);
    
    assert(false != fifo_task_resumed(sync));
}

/**
 * @brief Reset the fifo mutex module
 */
void fifo_mutex_reset(void)
{
	PRINT_MSG("%s Starting\r\n", __FUNCTION__);
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
            fifo_module_data.initialized = true;
            fifo_module_data.locked = false;
            fifo_module_data.sync_semaphore = NULL;
        }
        pthread_mutex_unlock(&fifo_module_data.init_mutex);
    }
}
