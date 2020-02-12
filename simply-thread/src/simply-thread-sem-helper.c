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
#include <simply-thread-log.h>


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
#define PRINT_MSG(...) printf(__VA_ARGS__)
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
    int sem_count = 0; //!< Variable that deals with the semaphore count
    const char *base_string = "simply_thread_semaphore_";
    char name[MAX_SEM_NAME_SIZE];
    assert(NULL != sem);
    do
    {
        assert(MAX_SEM_NUMBER > sem_count);
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
    assert(0 == sem_close(sem->sem));
    typed = sem->data;
    assert(NULL != typed);
    unlink_sem_by_name(typed->sem_name);
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
    rv = sem_wait(sem->sem);
    if(0 != rv)
    {
        eval = errno;
        if(EINTR != eval)
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
    assert(0 == rv);
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
    simply_thread_init_registery();
    for(unsigned int i = 0; i < MAX_SEM_COUNT; i++)
    {
        if(0 != memcmp(st_sem_registery.createded_sems[i].sem_name, st_empty_entry.sem_name, MAX_SEM_NAME_SIZE))
        {
            unlink_sem_by_name(st_sem_registery.createded_sems[i].sem_name);
        }
    }
}
