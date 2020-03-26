/**
 * @file Sem-Helper.c
 * @author Kade Cox
 * @date Created: Mar 19, 2020
 * @details
 *
 */

#include <simply-thread.h>
#include <simply-thread-log.h>
#include <Sem-Helper.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <que-creator.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifndef MAX_SEM_COUNT
#define MAX_SEM_COUNT 500
#endif //MAX_SEM_COUNT

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/


struct Sem_Helper_Module_Data_s
{
    pthread_t id;
    int queue_id;
    bool kill_worker;
    bool enabled;
    struct
    {
        int sem_ids[MAX_SEM_COUNT];
        bool initialized;
    } reg;
}; //!< Structure that holds this modules local data

struct sem_helper_message_data_s
{
    enum
    {
        SS_SEM_CREATE,
        SS_SEM_DESTROY,
        SS_SEM_CLEANUP,
        SS_SEM_KILL
    } action;
    sem_helper_sem_t *sem;
    bool *finished;
}; //!<Structure that holds message data

union sem_helper_union
{
    int val;
    struct semid_ds *buf;
    unsigned short  *array;
}; //!< union used to set semaphore parameters

struct sem_helper_raw_message_s
{
    long type;
    union
    {
        char msg[sizeof(struct sem_helper_message_data_s)];
        struct sem_helper_message_data_s decoded;
    };
}; //!<Raw message to send to the handler

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

struct Sem_Helper_Module_Data_s sem_module_data =
{
    .queue_id = -1,
    .kill_worker = false,
    .enabled = true,
    .reg = {
        .initialized = false
    }
}; //!< The modules local data
static struct sembuf sem_helper_sem_dec = { 0, -1, SEM_UNDO}; //used for dec
static struct sembuf sem_helper_sem_try_dec = { 0, -1, SEM_UNDO | IPC_NOWAIT}; //used to try dec
static struct sembuf sem_helper_sem_inc = { 0, +1, SEM_UNDO}; //used to inc

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


/**
 * Initialize the ipc message queue
 */
static inline int init_sm_msg_queue(void)
{
    return create_new_queue();
}

/**
 * Register a created semaphore
 * @param sem
 */
static inline void sem_helper_sem_register(sem_helper_sem_t *sem)
{
    bool finished = false;
    if(false == sem_module_data.reg.initialized)
    {
        for(int i = 0; i < ARRAY_MAX_COUNT(sem_module_data.reg.sem_ids); i++)
        {
            sem_module_data.reg.sem_ids[i] = -1;
        }
        sem_module_data.reg.initialized = true;
    }
    for(int i = 0; i < ARRAY_MAX_COUNT(sem_module_data.reg.sem_ids) && false == finished; i++)
    {
        if(-1 == sem_module_data.reg.sem_ids[i])
        {
            sem_module_data.reg.sem_ids[i] = sem->id;
            finished = true;
        }
    }
}

/**
 * Function that creates a semaphore as triggered by the message handler
 * @param sem
 */
static void internal_sem_create(sem_helper_sem_t *sem)
{
    int result;
    union sem_helper_union worker_union;
    //Ok need to create a new semaphore
    sem->id = semget(IPC_PRIVATE, 1, IPC_CREAT | IPC_EXCL | 0666);
    SS_ASSERT(sem->id >= 0);
    sem_helper_sem_register(sem);
    worker_union.val = 1;
    result = semctl(sem->id, 0, SETVAL, worker_union);
    if(0 > result)
    {
        while(1) {}
    }
    result = Sem_Helper_sem_trywait(sem);
    SS_ASSERT(0 == result);
    result = Sem_Helper_sem_trywait(sem);
    assert(EAGAIN == result);
}

/**
 * Internal function that deletes a semaphore
 * @param sem
 */
static void internal_sem_delete(sem_helper_sem_t *sem)
{
    int result;

    SS_ASSERT(NULL != sem);

    if(false == sem_module_data.reg.initialized)
    {
        for(int i = 0; i < ARRAY_MAX_COUNT(sem_module_data.reg.sem_ids); i++)
        {
            sem_module_data.reg.sem_ids[i] = -1;
        }
        sem_module_data.reg.initialized = true;
    }

    if(-1 != sem->id)
    {
        result = semctl(sem->id, 0, IPC_RMID);
        if(0 != result)
        {
            ST_LOG_ERROR("%s semctl returned %i with error %i\r\n", __FUNCTION__, result, errno);
            while(1) {}
        }
        //Remove the registered semaphore from the registry
        for(int i = 0; i < ARRAY_MAX_COUNT(sem_module_data.reg.sem_ids); i++)
        {
            if(sem->id == sem_module_data.reg.sem_ids[i])
            {
                sem_module_data.reg.sem_ids[i] = -1;
            }
        }
    }
}

/**
 * Internal function for clearing all registered semaphores
 */
static void internal_sem_clear(void)
{
    int result;
    int error_val;
    struct sem_helper_raw_message_s in_message;

    if(false == sem_module_data.reg.initialized)
    {
        for(int i = 0; i < ARRAY_MAX_COUNT(sem_module_data.reg.sem_ids); i++)
        {
            sem_module_data.reg.sem_ids[i] = -1;
        }
        sem_module_data.reg.initialized = true;
    }

    for(int i = 0; i < ARRAY_MAX_COUNT(sem_module_data.reg.sem_ids); i++)
    {
        if(-1 != sem_module_data.reg.sem_ids[i])
        {
            //Delete the existing semaphore
            semctl(sem_module_data.reg.sem_ids[i], 0, IPC_RMID);
            sem_module_data.reg.sem_ids[i] = -1;
        }
    }
    //Clear out any pending messages
    do
    {
        result = msgrcv(sem_module_data.queue_id, &in_message, sizeof(struct sem_helper_raw_message_s), 1, IPC_NOWAIT);
        if(0 > result)
        {
            error_val = errno;
        }
    }
    while(ENOMSG != error_val);
}

/**
 * Function for thread that handles incoming messages
 * @param data
 */
static void *sem_msg_handler_thread(void *data)
{
    struct sem_helper_raw_message_s in_message;
    int result;

    SS_ASSERT(NULL == data);
    while(false == sem_module_data.kill_worker)
    {
        result = msgrcv(sem_module_data.queue_id, &in_message, sizeof(struct sem_helper_raw_message_s), 1, 0);
        if(result != -1)
        {
            SS_ASSERT(1 == in_message.type);
            switch(in_message.decoded.action)
            {
                case SS_SEM_CREATE:
                    //We need to create a semaphore
                    internal_sem_create(in_message.decoded.sem);
                    break;
                case SS_SEM_DESTROY:
                    internal_sem_delete(in_message.decoded.sem);
                    break;
                case SS_SEM_CLEANUP:
                    internal_sem_clear();
                    break;
                case SS_SEM_KILL:
                    sem_module_data.kill_worker = true;
                    break;
                default:
                    SS_ASSERT(true == false);
                    break;
            }
            in_message.decoded.finished[0] = true;
        }
        else
        {
            ST_LOG_ERROR("%s error msgrcv failed with error %i\r\n", __FUNCTION__, errno);
            SS_ASSERT(true == false);
        }
    }
    if(msgctl(sem_module_data.queue_id, IPC_RMID, NULL) == -1)
    {
        ST_LOG_ERROR("Failed to delete my message queue\r\n");
    }
    sem_module_data.queue_id = -1;
    return NULL;
}

/**
 * Function that initializes the messaging thread
 */
static void init_msg_thread(void)
{
    if(-1 == sem_module_data.queue_id)
    {
        sem_module_data.kill_worker = false;
        sem_module_data.enabled = false;
        sem_module_data.queue_id = init_sm_msg_queue();
        SS_ASSERT(0 == pthread_create(&sem_module_data.id, NULL, sem_msg_handler_thread, NULL));
        sem_module_data.enabled = true;
    }
}

/**
 * @brief Initialize a semaphore
 * @param sem
 */
void Sem_Helper_sem_init(sem_helper_sem_t *sem)
{
    struct sem_helper_raw_message_s msg;
    bool comp = false;
    int result;

    SS_ASSERT(NULL != sem);
    SS_ASSERT(true == sem_module_data.enabled);

    init_msg_thread();
    msg.type = 1;
    msg.decoded.action = SS_SEM_CREATE;
    msg.decoded.finished = &comp;
    msg.decoded.sem = sem;

    result = msgsnd(sem_module_data.queue_id, &msg, sizeof(msg.decoded), 0);
    SS_ASSERT(0 == result);
    while(false == comp) {}
}

/**
 * Destroy a semaphore
 * @param sem
 */
void Sem_Helper_sem_destroy(sem_helper_sem_t *sem)
{
    struct sem_helper_raw_message_s msg;
    bool comp = false;
    int result;

    SS_ASSERT(NULL != sem);
    SS_ASSERT(true == sem_module_data.enabled);

    init_msg_thread();
    msg.type = 1;
    msg.decoded.action = SS_SEM_DESTROY;
    msg.decoded.finished = &comp;
    msg.decoded.sem = sem;

    result = msgsnd(sem_module_data.queue_id, &msg, sizeof(msg.decoded), 0);
    SS_ASSERT(0 == result);
    while(false == comp) {}
}

/**
 * Blocking Wait for a semaphor
 * @param sem
 * @return 0 on success
 */
int Sem_Helper_sem_wait(sem_helper_sem_t *sem)
{
    int rv;
    init_msg_thread();
    SS_ASSERT(NULL != sem);
    SS_ASSERT(true == sem_module_data.enabled);
    rv = semop(sem->id, &sem_helper_sem_dec, 1);
    return rv;
}

/**
 * Nonblocking semaphore wait
 * @param sem
 * @return 0 on success
 */
int Sem_Helper_sem_trywait(sem_helper_sem_t *sem)
{
    int rv;
    init_msg_thread();

    SS_ASSERT(NULL != sem);
    SS_ASSERT(true == sem_module_data.enabled);
    rv = semop(sem->id, &sem_helper_sem_try_dec, 1);
    if(0 != rv)
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
int Sem_Helper_sem_post(sem_helper_sem_t *sem)
{
    int rv;
    init_msg_thread();

    SS_ASSERT(NULL != sem);
    SS_ASSERT(true == sem_module_data.enabled);
    rv = semop(sem->id, &sem_helper_sem_inc, 1);
    if(0 != rv)
    {
        ST_LOG_ERROR("\t%s semop returned %i error %s\r\n", __FUNCTION__, rv, errno);
    }
    return rv;
}


/**
 * @brief Function that clears out all of the allocated semaphores
 */
void Sem_Helper_clear(void)
{
    struct sem_helper_raw_message_s msg;
    bool comp = false;
    int result;

    init_msg_thread();
    msg.type = 1;
    msg.decoded.action = SS_SEM_CLEANUP;
    msg.decoded.finished = &comp;
    msg.decoded.sem = NULL;

    result = msgsnd(sem_module_data.queue_id, &msg, sizeof(msg.decoded), 0);
    SS_ASSERT(0 == result);
    while(false == comp) {}
}

/**
 * Cleanup function to call on exit
 */
void Sem_Helper_clean_up(void)
{
    struct sem_helper_raw_message_s msg;
    bool comp = false;
    int result;

    init_msg_thread();
    sem_module_data.enabled = false;
    Sem_Helper_clear();

    msg.type = 1;
    msg.decoded.action = SS_SEM_KILL;
    msg.decoded.finished = &comp;
    msg.decoded.sem = NULL;

    result = msgsnd(sem_module_data.queue_id, &msg, sizeof(msg.decoded), 0);
    SS_ASSERT(0 == result);
    while(false == comp) {}
    pthread_join(sem_module_data.id, NULL);
    SS_ASSERT(-1 == sem_module_data.queue_id);
    sem_module_data.enabled = true;
}
