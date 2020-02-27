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
    simply_thread_sem_t *ticket; //!< The semaphore ticket
    pthread_t id; //!< The thread id of the thread that fetched the ticket
    bool *locked; //!< boolean value that tells if the ticket is currently locked
}; //!< Structure for a single ticket table entry

struct fifo_mutex_module_data_s
{
    bool initialized; //!< Boolean that tells if the module has been initialized.
    pthread_mutex_t init_mutex; //!< The mutex to protect the module initialization
    simply_thread_sem_t ticket_semaphore; //!< Semaphore that protects the module data
    struct ticket_entry_s ticket_table[MAX_LIST_SIZE]; //!< List of tickets used by the module
    simply_thread_sem_t sync_semaphore_get; //!< One of the synchronization semaphores
    simply_thread_sem_t sync_semaphore_release; //!< One of the synchronization semaphores
    bool locked;  //!< Tells if we are currently locked
    bool last_run_synced; //!< Debug Value that tells it the last release used a sync
    bool release_in_progress; //!< Debug value that tells if a release is in progress
    bool mutex_prepped; //!< Tells if the mutex is prepped
}; //!< Structure for holding the modules local data

struct push_pull_data_s
{
    struct ticket_entry_s entry;
}; //!< Structure for the push and pull actions


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
    .last_run_synced = false,
    .release_in_progress = false,
    .mutex_prepped = false
}; //!<Variable that holds the modules local data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


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
    for(int i = 0; i < ARRAY_MAX_COUNT(fifo_module_data.ticket_table); i++)
    {
        if(fifo_module_data.ticket_table[i].ticket != NULL && fifo_module_data.ticket_table[i].id == id)
        {
            entry_count++;
        }
    }
    assert(2 > entry_count);
    for(int i = 0; i < ARRAY_MAX_COUNT(fifo_module_data.ticket_table) && NULL == rv; i++)
    {
        if(fifo_module_data.ticket_table[i].ticket != NULL && fifo_module_data.ticket_table[i].id == id)
        {
            rv = &fifo_module_data.ticket_table[i];
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
    for(int i = 0; i < ARRAY_MAX_COUNT(fifo_module_data.ticket_table) && NULL == rv; i++)
    {
        if(fifo_module_data.ticket_table[i].ticket == NULL)
        {
            rv = &fifo_module_data.ticket_table[i];
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
    PRINT_MSG("\t%s Starting\r\n", __FUNCTION__);
    SEM_BLOCK(fifo_module_data.ticket_semaphore);
    PRINT_MSG("++++++%s has ticket semaphore\r\n", __FUNCTION__);
    id = pthread_self();
    existing = process_entry(id);
    next = NULL;
    assert(NULL == existing);
    next = next_available_entry();
    assert(NULL != next);
    next->id = id;
    next->ticket = new_ticket();
    next->locked = malloc(sizeof(bool));
    assert(NULL != next->locked);
    next->locked[0] = true;
    PRINT_MSG("\tNew Ticket: %p %s\r\n", next->ticket->sem, simply_thread_sem_get_filename(next->ticket));

    if(&fifo_module_data.ticket_table[0] == next && false == fifo_module_data.locked)
    {
        //We are the first entry on the table and are already locked
        PRINT_MSG("\tadded first entry\r\n");
        SEM_UNBLOCK(next->ticket[0]);
        next->locked[0] = false;
    }
    else
    {
        PRINT_MSG("\tadded non first entry\r\n");
    }
    entry.id = next->id;
    entry.ticket = next->ticket;
    entry.locked = next->locked;
    PRINT_MSG("------%s releasing ticket semaphore\r\n", __FUNCTION__);
    SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    return entry;
}

/**
 * Function that asserts if an entry got coorupted
 * @param ind
 */
static void check_ticket_entry(int ind)
{
    if(NULL != fifo_module_data.ticket_table[ind].ticket)
    {
        assert(NULL != fifo_module_data.ticket_table[ind].locked);
    }
}

/**
 * @brief Shift the entire ticket counter
 */
static void ticket_counter_shift(void)
{
    int returned;
    returned = simply_thread_sem_trywait(&fifo_module_data.ticket_semaphore);
    if(EAGAIN != returned)
    {
        ST_LOG_ERROR("simply_thread_sem_trywait returned %i\r\n", returned);
        assert(EAGAIN == returned);
    }
    for(int i = 1; i < ARRAY_MAX_COUNT(fifo_module_data.ticket_table); i++)
    {
        check_ticket_entry(i);
        check_ticket_entry(i + 1);
        fifo_module_data.ticket_table[i - 1].id = fifo_module_data.ticket_table[i].id;
        fifo_module_data.ticket_table[i - 1].ticket = fifo_module_data.ticket_table[i].ticket;
        fifo_module_data.ticket_table[i - 1].locked = fifo_module_data.ticket_table[i].locked;
    }
}

/**
 * @brief wait for our entries turn for the mutex
 * @param entry pointer to our entry
 */
static void fifo_mutex_wait_turn(struct ticket_entry_s *entry)
{
    bool sync_required;
    int rv = -1;

    sync_required = false;
    assert(NULL != entry);
    assert(NULL != entry->ticket);
    assert(NULL != entry->locked);
    PRINT_MSG("\t%s Starting\r\n", __FUNCTION__);
    sync_required = entry->locked[0];
    PRINT_MSG("\tWaiting on ticket %p %s\r\n", entry->ticket->sem, simply_thread_sem_get_filename(entry->ticket));
    while(0 != rv)
    {
        rv = simply_thread_sem_wait(entry->ticket);
        if(entry->locked[0] == false)
        {
            rv = 0;
            sync_required = false;
        }
    }

    if(false != fifo_module_data.locked)
    {
        ST_LOG_ERROR("Error the Fifo Mutex is already locked\r\n");
        assert(false);
    }
    PRINT_MSG("\tGot ticket %s\r\n", simply_thread_sem_get_filename(entry->ticket));
    if(true == sync_required)
    {
        PRINT_MSG("\t%s SyncRequired Performing sync\r\n", __FUNCTION__);
        SEM_UNBLOCK(fifo_module_data.sync_semaphore_release);
        SEM_BLOCK(fifo_module_data.sync_semaphore_get);
    }
    else
    {
        PRINT_MSG("\t%s No Sync Required\r\n", __FUNCTION__);
    }
    assert(false == fifo_module_data.locked);
    SEM_BLOCK(fifo_module_data.ticket_semaphore);
    PRINT_MSG("++++++%s has ticket semaphore\r\n", __FUNCTION__);
    assert(false == fifo_module_data.locked);

    fifo_module_data.locked = true;
    PRINT_MSG("\t%s destroying mutex %p %s\r\n", __FUNCTION__,
              entry->ticket->sem,
              simply_thread_sem_get_filename(entry->ticket));
    simply_thread_sem_destroy(entry->ticket);
    free(entry->ticket);
    free(entry->locked);
    PRINT_MSG("------%s releasing ticket semaphore\r\n", __FUNCTION__);
    SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    PRINT_MSG("Finished Waiting\r\n");
}


/**
 * @brief release the fifo mutex
 */
void fifo_mutex_release(void)
{
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    m_init_master_semaphore();
    assert(true == fifo_module_data.locked);
    // if(true == fifo_module_data.mutex_prepped)
    // {
    //  SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    //  return;
    // }
    assert(true == fifo_module_data.locked);
    assert(false == fifo_module_data.release_in_progress);
    fifo_module_data.release_in_progress = true;
    if(false == fifo_module_data.mutex_prepped)
    {
        SEM_BLOCK(fifo_module_data.ticket_semaphore);
    }
    assert(true == fifo_module_data.locked);
    PRINT_MSG("++++++%s has ticket semaphore\r\n", __FUNCTION__);
    fifo_module_data.locked = false;

    //We need to shift the ticket counter
    assert(NULL != fifo_module_data.ticket_table[0].ticket);

    fifo_module_data.ticket_table[0].ticket = NULL;
    fifo_module_data.ticket_table[0].locked = NULL;
    ticket_counter_shift();
    fifo_module_data.last_run_synced = false;
    if(NULL != fifo_module_data.ticket_table[0].ticket)
    {
        if(true == fifo_module_data.ticket_table[0].locked[0])
        {
            fifo_module_data.last_run_synced = true;
            assert(NULL != fifo_module_data.ticket_table[0].locked);
            assert(true == fifo_module_data.ticket_table[0].locked[0]);
            PRINT_MSG("\t%s Selecting Ticket %p %s\r\n", __FUNCTION__,
                      fifo_module_data.ticket_table[0].ticket->sem,
                      simply_thread_sem_get_filename(fifo_module_data.ticket_table[0].ticket));
            SEM_UNBLOCK(fifo_module_data.ticket_table[0].ticket[0]);
            SEM_BLOCK(fifo_module_data.sync_semaphore_release);
            SEM_UNBLOCK(fifo_module_data.sync_semaphore_get);
        }
    }
    fifo_module_data.release_in_progress = false;
    PRINT_MSG("------%s releasing ticket semaphore\r\n", __FUNCTION__);
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
    if(false == fifo_module_data.mutex_prepped)
    {
        SEM_BLOCK(fifo_module_data.ticket_semaphore);
        PRINT_MSG("++++++%s has ticket semaphore\r\n", __FUNCTION__);
        rv = fifo_module_data.locked;
        PRINT_MSG("------%s releasing ticket semaphore\r\n", __FUNCTION__);
        SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    }
    else
    {
        rv = fifo_module_data.locked;
    }
    return rv;
}

/**
 * @brief Function that waits for there to be no pending functions
 */
static inline void fifo_mutex_wait_clear(void)
{
    unsigned int used_count;

    PRINT_MSG("Waiting for clear\r\n");
    SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    PRINT_MSG("Got ticket semaphore\r\n");
    do
    {
        used_count = 0;
        SEM_BLOCK(fifo_module_data.ticket_semaphore);
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(fifo_module_data.ticket_table); i++)
        {
            if(NULL != fifo_module_data.ticket_table[i].ticket)
            {
                if(fifo_module_data.ticket_table[i].id != pthread_self())
                {
                    PRINT_MSG("killing thread\r\n");
                    pthread_kill(fifo_module_data.ticket_table[i].id, SIGUSR2);
                    PRINT_MSG("Joining thread\r\n");
                    pthread_join(fifo_module_data.ticket_table[i].id, NULL);
                    PRINT_MSG("joined\r\n");
                }
                fifo_module_data.ticket_table[i].ticket = NULL;
                used_count++;
            }
        }
        if(0 != used_count)
        {
            SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
        }
    }
    while(used_count != 0);
}

/**
 * @brief Reset the fifo mutex module
 */
void fifo_mutex_reset(void)
{
    int test;
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    if(true == fifo_module_data.initialized)
    {
        fifo_mutex_wait_clear();
    }
    assert(0 == pthread_mutex_lock(&fifo_module_data.init_mutex));
    PRINT_MSG("\tGot the init mutex\r\n");
    if(true == fifo_module_data.initialized)
    {
        test = simply_thread_sem_trywait(&fifo_module_data.ticket_semaphore);
        PRINT_MSG("\tTrywait returned: %i\r\n", test);
        if(EAGAIN != test)
        {
            assert(0 == test);
        }
        else
        {
            simply_thread_sem_post(&fifo_module_data.ticket_semaphore);
        }
        PRINT_MSG("\tdestroying fifo_module_data.ticket_semaphore\r\n");
        simply_thread_sem_destroy(&fifo_module_data.ticket_semaphore);
        PRINT_MSG("\tdestroying fifo_module_data.sync_semaphore_get\r\n");
        simply_thread_sem_destroy(&fifo_module_data.sync_semaphore_get);
        PRINT_MSG("\tdestroying fifo_module_data.sync_semaphore_release\r\n");
        simply_thread_sem_destroy(&fifo_module_data.sync_semaphore_release);
        fifo_module_data.initialized = false;
    }
    pthread_mutex_unlock(&fifo_module_data.init_mutex);
    PRINT_MSG("%s Finished\r\n", __FUNCTION__);
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
            simply_thread_sem_init(&fifo_module_data.sync_semaphore_get);
            simply_thread_sem_init(&fifo_module_data.sync_semaphore_release);
            assert(0 == simply_thread_sem_trywait(&fifo_module_data.sync_semaphore_get));
            assert(0 == simply_thread_sem_trywait(&fifo_module_data.sync_semaphore_release));

            for(unsigned int i = 0; i < ARRAY_MAX_COUNT(fifo_module_data.ticket_table); i++)
            {
                fifo_module_data.ticket_table[i].ticket = NULL;
            }
            fifo_module_data.initialized = true;
            fifo_module_data.locked = false;
            fifo_module_data.release_in_progress = false;
            fifo_module_data.mutex_prepped = false;
        }
        pthread_mutex_unlock(&fifo_module_data.init_mutex);
    }
}

/**
 * @brief Function that removes gaps from the table
 */
static void squash_gaps(void)
{
    bool squashed = true;
    while(true == squashed)
    {
        squashed = false;
        for(unsigned int i = 0; i < (ARRAY_MAX_COUNT(fifo_module_data.ticket_table) - 1); i++)
        {
            check_ticket_entry(i);
            check_ticket_entry(i + 1);
            if(fifo_module_data.ticket_table[i].ticket == NULL && NULL != fifo_module_data.ticket_table[i + 1].ticket)
            {
                assert(NULL != fifo_module_data.ticket_table[i + 1].locked);
                fifo_module_data.ticket_table[i].ticket = fifo_module_data.ticket_table[i + 1].ticket;
                fifo_module_data.ticket_table[i].id = fifo_module_data.ticket_table[i + 1].id;
                fifo_module_data.ticket_table[i].locked = fifo_module_data.ticket_table[i + 1].locked;
                fifo_module_data.ticket_table[i + 1].ticket = NULL;
                fifo_module_data.ticket_table[i + 1].locked = NULL;
                assert(NULL != fifo_module_data.ticket_table[i].locked);
                assert(NULL != fifo_module_data.ticket_table[i].ticket);
                squashed = true;
            }
        }
    }
}

/**
 * Pull the fifo entry off of the fifo queue for the current task
 * @return NULL if entry does not exist.
 */
fifo_mutex_entry_t fifo_mutex_pull(void)
{
    pthread_t id;
    int check_result;
    struct ticket_entry_s *existing;
    struct push_pull_data_s *save_data;
    save_data = NULL;
    id = pthread_self();
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    check_result = simply_thread_sem_trywait(&fifo_module_data.ticket_semaphore);
    assert(EAGAIN == check_result);
    PRINT_MSG("++++++%s has ticket semaphore\r\n", __FUNCTION__);
    existing = process_entry(id);
    if(NULL != existing)
    {
        save_data = malloc(sizeof(struct push_pull_data_s));
        assert(save_data != NULL);
        save_data->entry.id = existing->id;
        save_data->entry.ticket = existing->ticket;
        save_data->entry.locked = existing->locked;
        assert(id == save_data->entry.id);
        existing->ticket = NULL;
        squash_gaps();
        assert(NULL == process_entry(id));
        PRINT_MSG("!!!!!!!!!!! %s Pulled %p \r\n", __FUNCTION__, save_data->entry.ticket->sem);
    }
    PRINT_MSG("------%s releasing ticket semaphore\r\n", __FUNCTION__);
    PRINT_MSG("%s Ending\r\n", __FUNCTION__);
    SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    return (fifo_mutex_entry_t)save_data;
}

/**
 * @brief push a previously pulled entry back onto the fifo
 * @param entry
 */
void fifo_mutex_push(fifo_mutex_entry_t entry)
{
    pthread_t id;
    struct push_pull_data_s *typed;
    struct ticket_entry_s *existing;
    struct ticket_entry_s *next;
    assert(NULL != entry);
    PRINT_MSG("%s Starting\r\n", __FUNCTION__);
    id = pthread_self();
    typed = entry;
    SEM_BLOCK(fifo_module_data.ticket_semaphore);
    PRINT_MSG("++++++%s has ticket semaphore\r\n", __FUNCTION__);
    existing = process_entry(id);
    assert(NULL != existing); //We should have the master mutex
    next = next_available_entry();
    assert(NULL != next);
    PRINT_MSG("!!!!!!!!!!! %s Pushed on %p \r\n", __FUNCTION__, typed->entry.ticket->sem);
    next->id = typed->entry.id;
    next->ticket = typed->entry.ticket;
    next->locked = typed->entry.locked;
    if(next == &fifo_module_data.ticket_table[1])
    {
        PRINT_MSG("Next is the Next Entry\r\n");
        assert(EAGAIN == simply_thread_sem_trywait(next->ticket));
        assert(0 == simply_thread_sem_post(next->ticket));
        next->locked[0] = false;
    }
    free(typed);
    assert(true == fifo_module_data.locked);
    assert(false == fifo_module_data.release_in_progress);
    PRINT_MSG("------%s releasing ticket semaphore\r\n", __FUNCTION__);
    PRINT_MSG("%s Ending\r\n", __FUNCTION__);
    SEM_UNBLOCK(fifo_module_data.ticket_semaphore);
    assert(false == fifo_module_data.release_in_progress);
}

/**
 * Function that makes the fifo mutex safe to be interrupted
 */
void fifo_mutex_prep_signal(void)
{
    SEM_BLOCK(fifo_module_data.ticket_semaphore);
    PRINT_MSG("++++++%s has ticket semaphore\r\n", __FUNCTION__);
    fifo_module_data.mutex_prepped = true;
}

/**
 * Function That clears the prep flags
 */
void fifo_mutex_clear_prep_signal(void)
{
    fifo_module_data.mutex_prepped = false;
}


