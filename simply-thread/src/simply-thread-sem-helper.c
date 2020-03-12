/**
 * @file simply-thread-sem-helper.c
 * @author Kade Cox
 * @date Created: Jan 31, 2020
 * @details
 *
 */

#include <simply-thread-sem-helper.h>
#include <priv-simply-thread.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <simply-thread-log.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdint.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/


#ifndef MAX_SEM_NAME_SIZE
#define MAX_SEM_NAME_SIZE 50
#endif //MAX_SEM_NAME_SIZE

#ifndef MAX_SEM_COUNT
#define MAX_SEM_COUNT 500
#endif //MAX_SEM_COUNT

#define MAX_SEM_NUMBER 9000 //The max semaphore count allowed

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

//#define DEBUG_SIMPLY_THREAD

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_WHITE, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD



/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct simply_thread_sem_list_element_s
{
    int id;
}; //!< Structure for a single registry element

struct simply_thread_sem_registery_s
{
    struct simply_thread_sem_list_element_s createded_sems[MAX_SEM_COUNT]; //!< List of registry entries
    bool initialized; //!< Tells if the device has been initialized
}; //!< Structure that contains the data for the semaphore registry

struct simply_thread_timout_worker_data_s
{
    bool timed_out; //!< Tells if the sem wait timed out
    bool cont; //!< Tells if the worker should keep going
    unsigned int timeout_ms; //!< The max number of milliseconds to try and connect for
    simply_thread_sem_t *sem;  //!< The pointer to the waiting semaphore
};

union simply_thread_semaphore_union
{
    int val;
    struct semid_ds *buf;
    unsigned short  *array;
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/


/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct simply_thread_sem_registery_s st_sem_registery =
{
    .initialized = false
}; //!< Variable that holds the semaphore registry

static struct sembuf simply_thread_sem_dec = { 0, -1, SEM_UNDO};
static struct sembuf simply_thread_sem_try_dec = { 0, -1, SEM_UNDO | IPC_NOWAIT};
static struct sembuf simply_thread_sem_inc = { 0, +1, SEM_UNDO};

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/



/**
 * @brief Function that initializes the registry if required
 */
static void simply_thread_init_registery(void)
{
    if(false == st_sem_registery.initialized)
    {
        for(unsigned int i = 0; i < MAX_SEM_COUNT; i++)
        {
            st_sem_registery.createded_sems[i].id = -1;
        }
        st_sem_registery.initialized = true;
    }
}


/**
 * @brief Function that registers a created semaphore
 * @param name pointer to string with the semaphores name
 */
static void simply_thread_sem_register(simply_thread_sem_t *sem)
{
    simply_thread_init_registery();
    for(unsigned int i = 0; i < MAX_SEM_COUNT; i++)
    {
        if(-1 == st_sem_registery.createded_sems[i].id)
        {
            st_sem_registery.createded_sems[i].id = sem->id;
            sem->data = &st_sem_registery.createded_sems[i];
            return;
        }
    }
    SS_ASSERT(false); //We should never get here
}

/**
 * @brief Initialize a semaphore
 * @param sem
 */
void simply_thread_sem_init(simply_thread_sem_t *sem)
{
    int result;
    union simply_thread_semaphore_union worker_union;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != sem);
    sem->id = semget(IPC_PRIVATE, 1, IPC_CREAT | IPC_EXCL | 0666);
    SS_ASSERT(sem->id >= 0);
    PRINT_MSG("Created Semaphore %i\r\n", sem->id);
    simply_thread_sem_register(sem);
    worker_union.val = 1;
    SS_ASSERT(0 <= semctl(sem->id, 0, SETVAL, worker_union));
    PRINT_MSG("\t%s calling simply_thread_sem_trywait\r\n", __FUNCTION__);
    result = simply_thread_sem_trywait(sem);
    SS_ASSERT(0 == result);
    result = simply_thread_sem_trywait(sem);
    assert(EAGAIN == result);
}

/**
 * Destroy a semaphore
 * @param sem
 */
void simply_thread_sem_destroy(simply_thread_sem_t *sem)
{
    int result;
    struct simply_thread_sem_list_element_s *typed;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != sem);
    typed = sem->data;
    SS_ASSERT(typed != NULL);
    SS_ASSERT(typed->id == sem->id);
    if(-1 != sem->id)
    {
        result = semctl(sem->id, 0, IPC_RMID);
        if(0 != result)
        {
            ST_LOG_ERROR("\tsemctl returned %i error %i\r\n", result, errno);
        }
        typed->id = -1;
        PRINT_MSG("Destroyed semaphore: %i\r\n", sem->id);
    }
}

/**
 * Blocking Wait for a semaphor
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_wait(simply_thread_sem_t *sem)
{
    int rv;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != sem);
    rv = semop(sem->id, &simply_thread_sem_dec, 1);
    PRINT_MSG("%s returning %i with sem %i\r\n", __FUNCTION__, rv, sem->id);
    return rv;
}

/**
 * Nonblocking semaphore wait
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_trywait(simply_thread_sem_t *sem)
{
    int rv;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != sem);
    rv = semop(sem->id, &simply_thread_sem_try_dec, 1);
    if(0 != rv)
    {
        rv = errno;
    }
    PRINT_MSG("%s returning %i with sem %i\r\n", __FUNCTION__, rv, sem->id);
    return rv;
}

/**
 * Semaphore post
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_post(simply_thread_sem_t *sem)
{
    int rv;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != sem);
    rv = semop(sem->id, &simply_thread_sem_inc, 1);
    if(0 != rv)
    {
        ST_LOG_ERROR("\t%s semop returned %i error %s\r\n", __FUNCTION__, rv, errno);
    }
    PRINT_MSG("%s returning %i with sem %i\r\n", __FUNCTION__, rv, sem->id);
    return rv;
}

/**
 * @brief Function that unlinks created semaphores so they can be freed when tests complete
 */
void sem_helper_cleanup(void)
{
    int result;
    PRINT_MSG("%s\r\n", __FUNCTION__);
    simply_thread_init_registery();
    for(unsigned int i = 0; i < MAX_SEM_COUNT; i++)
    {
        if(-1 != st_sem_registery.createded_sems[i].id)
        {
            result = semctl(st_sem_registery.createded_sems[i].id, 0, IPC_RMID);
            if(result != 0)
            {
                PRINT_MSG("Sem Delete:\r\n\tSem: %i\r\n\tResult: %i\r\n\tErr: %i\r\n",
                          st_sem_registery.createded_sems[i].id, result, errno);
            }
        }
    }
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

    rv = (diff.us / us_per_ms) + (diff.s * ms_per_s);

    return rv;
}

/**
 * @brief worker task function for the timed sem wait
 * @param data
 */
static void *timed_worker(void *data)
{
    struct simply_thread_timout_worker_data_s *typed;
    struct timeval start;
    struct timeval current;
    unsigned int c_ms = 0;
    static const unsigned int sleep_time = ST_NS_PER_MS;
    typed = data;
    assert(NULL != typed);
    gettimeofday(&start, NULL);
    while(typed->cont == true)
    {
        simply_thread_sleep_ns(sleep_time);
        gettimeofday(&current, NULL);
        c_ms = time_diff(&start, &current);
        if(c_ms > typed->timeout_ms)
        {
            ST_LOG_ERROR("Sem Wait Timed out\r\n");
            typed->timed_out = true;
            typed->cont = false;
            assert(0 == simply_thread_sem_post(typed->sem));
        }
    }
    return NULL;
}

/**
 * @brief blocking semaphore wait with a timeout
 * @param sem
 * @param ms The max number of ms to wait for
 * @return o on success
 */
int simply_thread_sem_timed_wait(simply_thread_sem_t *sem, unsigned int ms)
{
    struct simply_thread_timout_worker_data_s *worker_data;
    pthread_t local_thread;
    int rv;

    assert(NULL != sem);
    assert(-1 != sem->id);
    assert(0 < ms);

    //TODO Remove this malloc
    worker_data = malloc(sizeof(struct simply_thread_timout_worker_data_s));
    assert(NULL != worker_data);
    worker_data->timeout_ms = ms;
    worker_data->cont = true;
    worker_data->timed_out = false;
    worker_data->sem = sem;

    assert(0 == pthread_create(&local_thread, NULL, timed_worker, worker_data));
    while(0 != simply_thread_sem_wait(sem)) {}
    if(true == worker_data->cont)
    {
        worker_data->cont = false;
    }
    pthread_join(local_thread, NULL);
    rv = -1;
    if(false == worker_data->timed_out)
    {
        rv = 0;
    }
    worker_data->sem = NULL;
    free(worker_data);
    return rv;
}

