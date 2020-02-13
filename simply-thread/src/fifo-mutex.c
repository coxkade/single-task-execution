/**
 * @file fifo-mutex.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 * A single fifo mutex
 */

#include <fifo-mutex.h>
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

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct ticket_entry_s{
    	int ticket;
    	pthread_t id;
    };

struct fifo_mutex_module_data_s
{
    bool initialized;
    pthread_mutex_t init_mutex;
    simply_thread_sem_t ticket_semaphore;
    simply_thread_sem_t process_semaphore[2]; //We may only need one of these
    struct ticket_entry_s ticket_table[MAX_LIST_SIZE];
    int ticket_counter;
    bool started;
    unsigned int ticket_count; //!< The number of tickets in use
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

static struct fifo_mutex_module_data_s fifo_module_data = {
    .init_mutex = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false
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
static struct ticket_entry_s * process_entry(pthread_t id)
{
	unsigned int entry_count;
	struct ticket_entry_s * rv;
	rv = NULL;
	entry_count = 0;
	for(int i=0; i<ARRAY_MAX_COUNT(M_DATA.ticket_table); i++)
	{
		if(M_DATA.ticket_table[i].ticket > (-1) && M_DATA.ticket_table[i].id == id)
		{
			entry_count++;
		}
	}
	assert(2 > entry_count);
	for(int i=0; i<ARRAY_MAX_COUNT(M_DATA.ticket_table) && NULL == rv; i++)
	{
		if(M_DATA.ticket_table[i].ticket > (-1) && M_DATA.ticket_table[i].id == id)
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
static struct ticket_entry_s * next_available_entry(void)
{
	struct ticket_entry_s * rv;
	rv = NULL;
	for(int i=0; i<ARRAY_MAX_COUNT(M_DATA.ticket_table) && NULL == rv; i++)
	{
		if(M_DATA.ticket_table[i].ticket == -1)
		{
			rv = &M_DATA.ticket_table[i];
		}
	}
	return rv;
}

/**
 * @brief function that checks if a ticket is in use
 * @param ticket the ticket to check
 * @return true if the ticket is in use
 */
static bool ticket_in_use(int ticket)
{
	for(int i=0; i<ARRAY_MAX_COUNT(M_DATA.ticket_table); i++)
	{
		if(ticket == M_DATA.ticket_table[i].ticket)
		{
			return true;
		}
	}
	return false;
}

/**
 * @brief function that fetches the next available ticket
 * @return the next available ticket
 */
static int next_ticket(void)
{
	while(true == ticket_in_use(fifo_module_data.ticket_counter))
	{
		fifo_module_data.ticket_counter++;
	}
	return fifo_module_data.ticket_counter;
}

/**
 * @brief fetch an execution ticket
 * @return The ticket dat you need
 */
static struct ticket_entry_s fifo_mutex_fetch_ticket(void)
{
	int ticket;
	struct ticket_entry_s * existing;
	struct ticket_entry_s * next;
	struct ticket_entry_s entry;
	pthread_t id;

	SEM_BLOCK(fifo_module_data.ticket_semaphore);
	id = pthread_self();
	ticket = next_ticket();
	existing = process_entry(id);
	next = next_available_entry();
	assert(NULL != next);

	if(NULL != existing)
	{
		assert(existing->id == id);
		next->id = existing->id;
		next->ticket = existing->ticket;
		existing->id = id;
		existing->ticket = ticket;
	}
	else
	{
		next->id = id;
		next->ticket = ticket;
	}
	entry.id = id;
	entry.ticket = ticket;
	SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
	return entry;
}

/**
 * @brief wait for our entries turn for the mutex
 * @param entry pointer to our entry
 */
static void fifo_mutex_wait_turn(struct ticket_entry_s * entry)
{
	bool myturn;
	myturn = false;
	assert(NULL != entry);
	while(false == myturn)
	{
		SEM_BLOCK(M_DATA.process_semaphore[0]);

        if(entry->ticket == M_DATA.ticket_table[0].ticket)
		{
            assert(entry->id == M_DATA.ticket_table[0].id);
			myturn = true;
			fifo_module_data.started = true;
			M_DATA.ticket_count++;
			assert(2>M_DATA.ticket_count);
		}

		SEM_UNBLOCK(M_DATA.process_semaphore[0]);
	}
}

/**
 * @brief fetch the mutex
 * @return true on success
 */ 
bool fifo_mutex_get(void)
{
	struct ticket_entry_s entry;
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
	assert(2>M_DATA.ticket_count);
	if(0 == M_DATA.ticket_count)
	{
		rv = false;
	}
	SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
	return rv;
}

/**
 * @brief Shift the entire ticket counter
 */
static void ticket_counter_shift(void)
{
	for(int i=1; i<ARRAY_MAX_COUNT(M_DATA.ticket_table); i++)
	{
		M_DATA.ticket_table[i-1].id = M_DATA.ticket_table[i].id;
		M_DATA.ticket_table[i-1].ticket = M_DATA.ticket_table[i].ticket;
	}
}

/**
 * @brief function that verifies a new task started running
 * @return true if a new task started running
 */
static bool fifo_task_resumed(void)
{
	static const unsigned int ns_period = 1000000/2;
	static const unsigned int timeout_count = 100*2;
	unsigned int current_count = 0;
	while(false == fifo_module_data.started)
	{
		 fifo_mutex_sleep_ns(ns_period);
		 current_count++;
		 if(current_count >= timeout_count)
		 {
			 printf("*******Task Resume Timed Out *********\r\n");
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
    m_init_master_semaphore();
    do
    {
		SEM_BLOCK(M_DATA.process_semaphore[0]);
		SEM_BLOCK(M_DATA.ticket_semaphore);
		//We need to shift the ticket counter
		M_DATA.ticket_table[0].ticket = -1;
		ticket_counter_shift();
		if(-1 != M_DATA.ticket_table[0].ticket)
		{
			fifo_module_data.started = false;
		}
		else
		{
			fifo_module_data.started = true;
		}
		M_DATA.ticket_count--;
		assert(2>M_DATA.ticket_count);
		SEM_UNBLOCK(M_DATA.process_semaphore[0]);
		SEM_UNBLOCK(M_DATA.ticket_semaphore);
    }while(false == fifo_task_resumed());
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
        simply_thread_sem_destroy(&fifo_module_data.process_semaphore[0]);
        simply_thread_sem_destroy(&fifo_module_data.process_semaphore[1]);
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
            simply_thread_sem_init(&fifo_module_data.process_semaphore[0]);
            simply_thread_sem_init(&fifo_module_data.process_semaphore[1]);

            for(unsigned int i=0; i<ARRAY_MAX_COUNT(fifo_module_data.ticket_table); i++)
            {
            	fifo_module_data.ticket_table[i].ticket = -1;
            }
            fifo_module_data.ticket_count = 0;
            fifo_module_data.ticket_counter = 0;
            fifo_module_data.started = false;
            fifo_module_data.initialized = true;
        }
        pthread_mutex_unlock(&fifo_module_data.init_mutex);
    }
}
