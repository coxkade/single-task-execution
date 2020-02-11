/**
 * @file simply-thread-sem-helper.c
 * @author Kade Cox
 * @date Created: Jan 31, 2020
 * @details
 *
 */

#include <simply-thread-sem-helper.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>


/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

#ifndef MAX_SEM_NAME_SIZE
#define MAX_SEM_NAME_SIZE 50
#endif //MAX_SEM_NAME_SIZE

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) printf(__VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


/**
 * @brief Initialize a semaphore
 * @param sem
 */
void simply_thread_sem_init(simply_thread_sem_t *sem)
{
    int sem_count = 0; //!< Variable that deals with the semaphore count
    const char *base_string = "simply_thread_semaphore_";
    char name[MAX_SEM_NAME_SIZE];
    assert(NULL != sem);
    do
    {
        snprintf(name, MAX_SEM_NAME_SIZE, "%s%i", base_string, sem_count++);
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
    }
    while(SEM_FAILED == sem->sem);
    PRINT_MSG("Created Semaphore: %s\r\n", name);
    sem->count = 1;
}

/**
 * Destroy a semaphore
 * @param sem
 */
void simply_thread_sem_destroy(simply_thread_sem_t *sem)
{
    assert(NULL != sem);
    assert(NULL != sem->sem);
    assert(0 == sem_close(sem->sem));
}

/**
 * Get a semaphores count
 * @param sem
 * @return the current semaphore count
 */
int simply_thread_sem_get_count(simply_thread_sem_t *sem)
{
    assert(NULL != sem);
    assert(NULL != sem->sem);
    return sem->count;
}

/**
 * Blocking Wait for a semaphor
 * @param sem
 * @return 0 on success
 */
int simply_thread_sem_wait(simply_thread_sem_t *sem)
{
    int rv;
    assert(NULL != sem);
    assert(NULL != sem->sem);
    rv = sem_wait(sem->sem);
    if(0 == rv && sem->count < 1)
    {
        sem->count = 1;
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
        if(sem->count < 1)
        {
            sem->count = 1;
        }
    }
    else
    {
        rv = errno;
    }

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
    assert(NULL != sem);
    assert(NULL != sem->sem);
    rv = sem_post(sem->sem);
    if(0 == rv && sem->count > 0)
    {
        sem->count = 0;
    }
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
        PRINT_MSG("Semaphore %s unlinked\r\n", name);
    }
}

/**
 * @brief Function that unlinks created semaphores so they can be freed when tests complete
 */
void sem_helper_cleanup(void)
{
    static const char *base_string = "simply_thread_semaphore_";
    static char name[MAX_SEM_NAME_SIZE];
    int worker_count = 0;

    do
    {
        snprintf(name, MAX_SEM_NAME_SIZE, "%s%i", base_string, worker_count);
        unlink_sem_by_name(name);
        worker_count++;
    }
    while(1000000 > worker_count);
}
