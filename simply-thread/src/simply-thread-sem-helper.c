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
    char sem_name[MAX_SEM_NAME_SIZE];
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

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/**
 * @brief check if a semaphore exists if so kill it
 * @param name
 */
static void unlink_sem_by_name(const char *name);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct simply_thread_sem_registery_s st_sem_registery =
{
    .initialized = false
}; //!< Variable that holds the semaphore registry

static const struct simply_thread_sem_list_element_s st_empty_entry =
{
    .sem_name = "UNUSED"
};//!< Variable that holds an unused registery entry

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
            memcpy(st_sem_registery.createded_sems[i].sem_name, st_empty_entry.sem_name, MAX_SEM_NAME_SIZE);
        }
        st_sem_registery.initialized = true;
    }
}

/**
 * @brief Function that registers a created semaphore
 * @param name pointer to string with the semaphores name
 */
static void simply_thread_sem_register(char *name, simply_thread_sem_t *sem)
{
    simply_thread_init_registery();
    assert(NULL != name);
    for(unsigned int i = 0; i < MAX_SEM_COUNT; i++)
    {
        if(0 == memcmp(st_sem_registery.createded_sems[i].sem_name, st_empty_entry.sem_name, MAX_SEM_NAME_SIZE))
        {
            memcpy(st_sem_registery.createded_sems[i].sem_name, name, MAX_SEM_NAME_SIZE);
            sem->data = &st_sem_registery.createded_sems[i];
            return;
        }
    }
    assert(false); //We should never get here
}

/**
 * @brief Initialize a semaphore
 * @param sem
 */
void simply_thread_sem_init(simply_thread_sem_t *sem)
{
    static const int max_sem_count = 1000;
    static int sem_count = 0; //!< Variable that deals with the semaphore count
    static const char *base_string = "simply_thread_semaphore_";
    char name[MAX_SEM_NAME_SIZE];
    assert(NULL != sem);
    do
    {
        assert(MAX_SEM_NUMBER > sem_count);
        simply_thread_snprintf(name, MAX_SEM_NAME_SIZE, "%s%i", base_string, sem_count);
        sem->sem = sem_open((const char *)name, O_CREAT | O_EXCL, 0700, 1);
        if(SEM_FAILED == sem->sem)
        {
            switch(errno)
            {
                case EEXIST:
                    break;
                case EACCES:
                    PRINT_MSG("Failed to create sem: %s EACCES\r\n", name);
                    assert(false);
                    break;
                case EINVAL:
                    PRINT_MSG("Failed to create sem: %s EINVAL\r\n", name);
                    assert(false);
                    break;
                case EMFILE:
                    PRINT_MSG("Failed to create sem: %s EMFILE\r\n", name);
                    assert(false);
                    break;
                default:
                    PRINT_MSG("Failed to create sem: %s %u\r\n", name, errno);
                    assert(false);
                    break;
            }
        }
        sem_count++;
        if(max_sem_count < sem_count)
        {
            sem_count = 0;
        }
    }
    while(SEM_FAILED == sem->sem);
    PRINT_MSG("Created semaphore: %p %s\r\n", sem->sem, name);
    simply_thread_sem_register(name, sem);
}

/**
 * Destroy a semaphore
 * @param sem
 */
void simply_thread_sem_destroy(simply_thread_sem_t *sem)
{
    struct simply_thread_sem_list_element_s *typed;
    assert(NULL != sem);
    assert(NULL != sem->sem);
    typed = sem->data;
    assert(NULL != typed);
    PRINT_MSG("Closing: %p %s\r\n", sem->sem, typed->sem_name);
    sem_trywait(sem->sem);
    if(0 != sem_close(sem->sem))
    {
        ST_LOG_ERROR("ERROR! %u Failed to close semaphore %s\r\n", errno, typed->sem_name);
        assert(false);
    }
    PRINT_MSG("%s Calling unlink_sem_by_name\r\n", __FUNCTION__);
    unlink_sem_by_name(typed->sem_name);
    PRINT_MSG("Closed: %p %s\r\n", sem->sem, typed->sem_name);
    memcpy(typed->sem_name, st_empty_entry.sem_name, MAX_SEM_NAME_SIZE);
}

/**
 * Blocking Wait for a semaphor
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_wait(simply_thread_sem_t *sem)
{
    int rv;
    int eval;
    assert(NULL != sem);
    assert(NULL != sem->sem);
    PRINT_MSG("%X Waiting on 0x%04X %s\r\n", pthread_self(), sem->sem, ((struct simply_thread_sem_list_element_s *)sem->data)->sem_name);
    rv = sem_wait(sem->sem);
    if(0 != rv)
    {
        eval = errno;
        if(EBADF == eval)
        {
            ST_LOG_ERROR("Bad File Descriptor: %s\r\n", simply_thread_sem_get_filename(sem));
        }
        if(EAGAIN == eval)
        {
            ST_LOG_ERROR("Error EAGAIN: %s\r\n", simply_thread_sem_get_filename(sem));
        }
        if(EINTR != eval && EAGAIN != eval)
        {
            ST_LOG_ERROR("Unsupported error %u\r\n", eval);
            assert(false);
        }
    }
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
    int result;
    assert(NULL != sem);
    assert(NULL != sem->sem);
    result = sem_trywait(sem->sem);
    if(0 == result)
    {
        rv = 0;
    }
    else
    {
        rv = errno;
    }
    PRINT_MSG("%s returning %i with sem %s\r\n", __FUNCTION__, rv, ((struct simply_thread_sem_list_element_s *)sem->data)->sem_name);
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
    int errorval;
    assert(NULL != sem);
    assert(NULL != sem->sem);
    PRINT_MSG("%X Posting to 0x%04X %s\r\n", pthread_self(), sem->sem, ((struct simply_thread_sem_list_element_s *)sem->data)->sem_name);
    do
    {
        rv = sem_post(sem->sem);
        if(0 != rv)
        {
            errorval = errno;
            assert(EOVERFLOW != errorval);
            assert(EINVAL != errorval);
        }
    }
    while(0 != rv);

    return rv;
}

/**
 * @brief check if a semaphore exists if so kill it
 * @param name
 */
static void unlink_sem_by_name(const char *name)
{
    if(0 != sem_unlink(name))
    {
        switch(errno)
        {
            case EACCES:
                PRINT_MSG("EACCES: %s\r\n", name);
                break;
            case ENAMETOOLONG:
                PRINT_MSG("ENAMETOOLONG: %s\r\n", name);
                assert(false);
                break;
            case EINVAL:
                //No semaphore by that name
                break;
            case ENOENT:
                //No semaphore by that name
                break;
            default:
                PRINT_MSG("Unknown Error Number %i\r\n", errno);
                assert(false);
                break;
        }
    }
    else
    {
        PRINT_MSG("Unlinked %s\r\n", name);
    }
}

/**
 * @brief Function that unlinks created semaphores so they can be freed when tests complete
 */
void sem_helper_cleanup(void)
{
    simply_thread_init_registery();
    for(unsigned int i = 0; i < MAX_SEM_COUNT; i++)
    {
        if(0 != memcmp(st_sem_registery.createded_sems[i].sem_name, st_empty_entry.sem_name, MAX_SEM_NAME_SIZE))
        {
            unlink_sem_by_name(st_sem_registery.createded_sems[i].sem_name);
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
    assert(NULL != sem->sem);
    assert(0 < ms);

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

/**
 * @brief Function that fetches the filename of the semaphore
 * @param sem pointer to the file name
 * @return
 */
const char *simply_thread_sem_get_filename(simply_thread_sem_t *sem)
{
    struct simply_thread_sem_list_element_s *typed;
    const char *rv;

    assert(sem != NULL);
    assert(sem->data != NULL);
    assert(sem->sem != NULL);

    typed = sem->data;
    rv = NULL;

    rv = typed->sem_name;

    return rv;
}
