/**
 * @file simply-thread-queue.c
 * @author Kade Cox
 * @date Created: Jan 23, 2020
 * @details
 * Implementation of the Queue Functions
 */

#include <simply-thread-queue.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <simply-thread.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

#ifndef SIMPLY_THREAD_QUEUE_DEBUG
#ifdef DEBUG_SIMPLY_THREAD
#define SIMPLY_THREAD_QUEUE_DEBUG
#endif //DEBUG_SIMPLY_THREAD
#endif //SIMPLY_THREAD_QUEUE_DEBUG

#ifdef SIMPLY_THREAD_QUEUE_DEBUG
#define PRINT_MSG(...) simply_thread_log(COLOR_PURPLE, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //SIMPLY_THREAD_QUEUE_DEBUG

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

//The maximum number of supported queues
#ifndef SIMPLE_THREAD_MAX_QUEUE
#define SIMPLE_THREAD_MAX_QUEUE (50)
#endif //SIMPLE_THREAD_MAX_QUEUE

//Helper cast macro
#define ST_CAST_THREAD(x) ((struct simply_thread_queue_data_s *)x)

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

enum simply_thread_block_reason_e
{
    SIMPLY_THREAD_QUEUE_WAIT_ADD, //!< Waiting for an item to be on the queue
    SIMPLY_THREAD_QUEUE_WAIT_REMOVE, //!< Waiting on an item to be off of the queue
};

struct simply_thread_queue_element_s
{
    bool in_use; //!< Tells if the element is in use
    void *data;  //!< Data of the element
};

struct simply_thread_queue_data_s
{
    unsigned int current_count; //!< The Current Queue Count
    unsigned int max_count; //!< The max number of elements allowed
    unsigned int data_size; //!< The size of each element
    const char *name;  //!< The name of the queue
    struct simply_thread_queue_element_s q_data[SIMPLE_THREAD_MAX_QUEUE];  //!< The actual queue data
    struct
    {
        enum simply_thread_block_reason_e reason; //!< The reason for the block
        struct simply_thread_task_s *task;  //!<Pointer to the blocked task
    } wait_list[SIMPLE_THREAD_MAX_QUEUE]; //!< Structure that holds a list of tasks waiting on the queue
}; //!< Structure that holds the data for individual queues

struct simply_thread_queue_module_data_s
{
    struct simply_thread_queue_data_s *queue_list[SIMPLE_THREAD_MAX_QUEUE];
    struct
    {
        bool in_use; //!< Tells if this table entry is in use
        struct simply_thread_task_s *task;  //!< The blocked task
        unsigned int max_count; //!< The max wait time in ms
        unsigned int current_count; //!< The current wait time in ms
        bool result; //!< The result of the queue operation
    } block_list[SIMPLE_THREAD_MAX_QUEUE];
}; //!<structure that holds the data local to the module

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/**
 * @brief Function that resumes tasks waiting on queues
 */
static void simply_thread_resume_queues(void);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct simply_thread_queue_module_data_s m_queue_data; //!< Structure that holds the data for this module

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief Function that initializes the simply thread queue module
 */
void simply_thread_queue_init(void)
{
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.queue_list); i++)
    {
        m_queue_data.queue_list[i] = NULL;
    }
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.block_list); i++)
    {
        m_queue_data.block_list[i].in_use = false;
        m_queue_data.block_list[i].current_count = 0;
        m_queue_data.block_list[i].max_count = 0xFFFFFFFF;
        m_queue_data.block_list[i].task = NULL;
    }
}

/**
 * @brief Function that cleans up the simply thread queue
 */
void simply_thread_queue_cleanup(void)
{
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.queue_list); i++)
    {
        if(NULL != m_queue_data.queue_list[i])
        {
            for(unsigned int j = 0; j < ARRAY_MAX_COUNT(m_queue_data.queue_list[i]->q_data); j++)
            {
                if(true == m_queue_data.queue_list[i]->q_data[j].in_use)
                {
                    m_queue_data.queue_list[i]->q_data[j].in_use = false;
                    free(m_queue_data.queue_list[i]->q_data[j].data);
                    m_queue_data.queue_list[i]->q_data[j].data = NULL;
                }
            }
            free(m_queue_data.queue_list[i]);
        }
        m_queue_data.queue_list[i] = NULL;
    }
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.block_list); i++)
    {
        m_queue_data.block_list[i].in_use = false;
        m_queue_data.block_list[i].current_count = 0;
        m_queue_data.block_list[i].max_count = 0xFFFFFFFF;
        m_queue_data.block_list[i].task = NULL;
    }
}

/**
 * @brief function the systic needs to call
 */
void simply_thread_queue_maint(void)
{
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.block_list); i++)
    {
        if(true == m_queue_data.block_list[i].in_use)
        {
            m_queue_data.block_list[i].current_count++;
        }
    }
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.block_list); i++)
    {
        if(true == m_queue_data.block_list[i].in_use)
        {
            assert(NULL != m_queue_data.block_list[i].task);
            if(m_queue_data.block_list[i].current_count >= m_queue_data.block_list[i].max_count)
            {
                PRINT_MSG("\ttask: %p\r\n", m_queue_data.block_list[i].task);
                PRINT_MSG("\t%s %s timed out\r\n", __FUNCTION__, m_queue_data.block_list[i].task->name);
                if(SIMPLY_THREAD_TASK_BLOCKED == m_queue_data.block_list[i].task->state)
                {
                    m_queue_data.block_list[i].result = false;
                    simply_thread_set_task_state_from_locked(m_queue_data.block_list[i].task, SIMPLY_THREAD_TASK_READY);
                    assert(true == simply_thread_master_mutex_locked()); //We must be locked
                }
            }
        }
    }
}

/**
 * @brief Function that creates a queue
 * @param name String containing the name of the queue
 * @param queue_size The number of elements allowed in the queue
 * @param element_size The size of each element in the queue
 * @return NULL on error.  Otherwise the created Queue Handle
 */
simply_thread_queue_t simply_thread_queue_create(const char *name, unsigned int queue_size, unsigned int element_size)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    bool queue_added;
    struct simply_thread_queue_data_s *new_queue;

    if(NULL == name || 0 == queue_size || 0 == element_size)
    {
        return NULL; //Parameters not valid
    }

    new_queue = malloc(sizeof(struct simply_thread_queue_data_s));
    assert(NULL != new_queue);
    new_queue->current_count = 0;
    new_queue->data_size = element_size;
    new_queue->max_count = queue_size;
    new_queue->name = name;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(new_queue->q_data); i++)
    {
        new_queue->q_data[i].data = NULL;
        new_queue->q_data[i].in_use = false;
    }
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(new_queue->wait_list); i++)
    {
        new_queue->wait_list[i].task = NULL;
    }
    queue_added = false;
    MUTEX_GET();
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.queue_list) && false == queue_added; i++)
    {
        if(NULL == m_queue_data.queue_list[i])
        {
            m_queue_data.queue_list[i] = new_queue;
            queue_added = true;
        }
    }
    MUTEX_RELEASE();
    assert(true == queue_added);
    return new_queue;
}

/**
 * @brief Function that gets the current number of elements on the queue
 * @param queue handle of the queue in question
 * @return 0xFFFFFFFF on error. Otherwise the queue count
 */
unsigned int simply_thread_queue_get_count(simply_thread_queue_t queue)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    static const unsigned int error_val = 0xFFFFFFFF;
    unsigned int rv;
    rv = error_val;
    if(NULL == queue)
    {
        return rv; //!< Invalid paramater
    }
    MUTEX_GET();
    rv = ST_CAST_THREAD(queue)->current_count;
    MUTEX_RELEASE();
    return rv;
}

/**
 * @brief Function that asserts if a task is already blocked
 * @param task The task to check
 * @param queue the queue to check
 */
static void simply_thread_assert_task_not_blocked(struct simply_thread_task_s *task, struct simply_thread_queue_data_s *queue)
{
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.block_list); i++)
    {
        if(true == m_queue_data.block_list[i].in_use)
        {
            assert(NULL != m_queue_data.block_list[i].task);
            assert(m_queue_data.block_list[i].task != task);
        }
    }
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(queue->wait_list); i++)
    {
        assert(task != queue->wait_list[i].task);
    }
}

/**
 * @brief Function that adds a task to a queues blocklist
 * @param queue queue to add the task to
 * @param task  the task to add to the waitlist
 * @param reason the reason for the block
 */
static void simply_thread_add_task_to_queue_blocklist(struct simply_thread_queue_data_s *queue, struct simply_thread_task_s *task,
        enum simply_thread_block_reason_e reason)
{
    bool added;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    assert(NULL != queue && NULL != task);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(queue->wait_list); i++)
    {
        assert(task != queue->wait_list[i].task);
    }
    added = false;
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(queue->wait_list) && false == added; i++)
    {
        if(NULL == queue->wait_list[i].task)
        {
            added = true;
            queue->wait_list[i].task =  task;
            queue->wait_list[i].reason = reason;
        }
    }
    assert(true == added);
}

/**
 * @brief Function that adds blocked task to the maint block list
 * @param task the task to add
 * @param block_time how long to block for
 * @return the maintinence block index used
 */
static unsigned int simply_thread_add_task_to_maint_block_list(struct simply_thread_task_s *task, unsigned int block_time)
{
    unsigned int rv = 0;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    assert(NULL != task && 0 != block_time);
    //Check that the task is not on the block list
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.block_list); i++)
    {
        if(true == m_queue_data.block_list[i].in_use)
        {
            assert(task != m_queue_data.block_list[i].task);
        }
    }
    //Add the task and queue to the blocklist
    rv = ARRAY_MAX_COUNT(m_queue_data.block_list);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.block_list) && ARRAY_MAX_COUNT(m_queue_data.block_list) == rv; i++)
    {
        if(false == m_queue_data.block_list[i].in_use)
        {
            m_queue_data.block_list[i].task = task;
            m_queue_data.block_list[i].max_count = block_time;
            m_queue_data.block_list[i].current_count = 0;
            m_queue_data.block_list[i].result = true;
            m_queue_data.block_list[i].in_use = true;
            rv = i;
        }
    }
    assert(ARRAY_MAX_COUNT(m_queue_data.block_list) != rv);
    PRINT_MSG("\t%s added to blocklist %p\r\n", task->name, task);
    return rv;
}

/**
 * @brief function that cleans up the data once a queue stops blocking
 * @param queue the queue in question
 * @param task the task in question
 * @param index_used the index of the maint block that was used
 */
static void simply_thread_queue_block_cleanup(struct simply_thread_queue_data_s *queue, struct simply_thread_task_s *task, unsigned int index_used)
{
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    assert(NULL != queue && NULL != task);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(queue->wait_list); i++)
    {
        if(task == queue->wait_list[i].task)
        {
            queue->wait_list[i].task = NULL;
        }
    }
    PRINT_MSG("\t%s cleaning up %s %p\r\n", __FUNCTION__, task->name, task);
    assert(true == m_queue_data.block_list[index_used].in_use);
    m_queue_data.block_list[index_used].task = NULL;
    m_queue_data.block_list[index_used].in_use = false;
}

/**
 * @brief Function that blocks until the send or recieve condition is met
 * @param queue the queue causing the block
 * @param task the task to block
 * @param block_time how long to block
 * @param reason the reason for the block
 * @return true
 * @return false
 */
static bool simply_thread_queue_lockdown(struct simply_thread_queue_data_s *queue, struct simply_thread_task_s *task, unsigned int block_time,
        enum simply_thread_block_reason_e reason)
{
    bool rv;
    unsigned int index_used = 0xFFFFFFFF;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    assert(NULL != queue);
    if(SIMPLY_THREAD_QUEUE_WAIT_REMOVE == reason)
    {
        assert(ST_CAST_THREAD(queue)->current_count == ST_CAST_THREAD(queue)->max_count);
    }
    if(SIMPLY_THREAD_QUEUE_WAIT_ADD == reason)
    {
        assert(0 == ST_CAST_THREAD(queue)->current_count);
    }
    if(0 == block_time)
    {
        return false;
    }
    assert(NULL != task);
    simply_thread_assert_task_not_blocked(task, queue);
    simply_thread_add_task_to_queue_blocklist(queue, task, reason);
    index_used = simply_thread_add_task_to_maint_block_list(task, block_time);
    PRINT_MSG("\t%s task %s waiting %p\r\n", __FUNCTION__, task->name, task);
    simply_thread_set_task_state_from_locked(task, SIMPLY_THREAD_TASK_BLOCKED);
    PRINT_MSG("\t%s task %s finished waiting \r\n", __FUNCTION__, task->name);
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    rv = m_queue_data.block_list[index_used].result;
    simply_thread_queue_block_cleanup(queue, task, index_used);
    return rv;
}

/**
 * @brief Function that clears all spaces from the queues data
 * @param queue the queue
 */
static void simply_thread_queue_data_cleanup(struct simply_thread_queue_data_s *queue)
{
    struct simply_thread_queue_element_s temp;
    assert(NULL != queue);
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    for(unsigned int i = 0; i < (ARRAY_MAX_COUNT(queue->q_data) - 1); i++)
    {
        if(false == queue->q_data[i].in_use && true == queue->q_data[i + 1].in_use)
        {
            //shift required
            memcpy(&temp, &queue->q_data[i], sizeof(temp));
            queue->q_data[i].data = queue->q_data[i + 1].data;
            queue->q_data[i].in_use = true;
            queue->q_data[i + 1].in_use = temp.in_use;
            queue->q_data[i + 1].data = temp.data;
        }
    }
}

/**
 * @brief Function that actually sends the data
 * @param queue The queue to add the data to
 * @param data Pointer to the data to add
 */
static void simply_thread_queue_data_add(struct simply_thread_queue_data_s *queue, void *data)
{
    bool data_saved;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    assert(NULL != data && NULL != queue);
    assert(queue->current_count < queue->max_count);
    data_saved = false;
    for(unsigned int i = 0; i < queue->max_count && false == data_saved; i++)
    {
        if(false == queue->q_data[i].in_use)
        {
            assert(NULL == queue->q_data[i].data);
            queue->q_data[i].data = malloc(queue->data_size);
            assert(NULL != queue->q_data[i].data);
            memcpy(queue->q_data[i].data, data, queue->data_size);
            queue->q_data[i].in_use = true;
            data_saved = true;
        }
    }
    assert(true == data_saved);
    simply_thread_queue_data_cleanup(queue);
    queue->current_count++;
}

/**
 * @brief remove data from the queue
 * @param queue the queue to get data from
 * @param data pointer to place the data into
 */
static void simply_thread_queue_data_remove(struct simply_thread_queue_data_s *queue, void *data)
{
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    assert(NULL != data && NULL != queue);
    assert(0 < queue->current_count);
    assert(true == queue->q_data[0].in_use);
    assert(NULL != queue->q_data[0].data);
    memcpy(data, queue->q_data[0].data, queue->data_size);
    queue->q_data[0].in_use = false;
    free(queue->q_data[0].data);
    queue->q_data[0].data = NULL;
    simply_thread_queue_data_cleanup(queue);
    queue->current_count--;
}

/**
 * @brief Function that places data on the queue
 * @param queue handle of the queue in question
 * @param data ptr to the data to place on the queue
 * @param block_time how long to wait on the queue
 * @return true on success, false otherwise
 */
bool simply_thread_queue_send(simply_thread_queue_t queue, void *data, unsigned int block_time)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    struct simply_thread_task_s *c_task;
    bool rv;

    if(NULL == queue || NULL == data)
    {
        return false;
    }
    rv = true;
    MUTEX_GET();
    c_task = simply_thread_get_ex_task();
    assert(ST_CAST_THREAD(queue)->current_count <= ST_CAST_THREAD(queue)->max_count);
    if(NULL == c_task)
    {
        block_time = 0; //In the interrupt context we cannot block
    }

    if(ST_CAST_THREAD(queue)->max_count == ST_CAST_THREAD(queue)->current_count)
    {
        rv = simply_thread_queue_lockdown(ST_CAST_THREAD(queue), c_task, block_time, SIMPLY_THREAD_QUEUE_WAIT_REMOVE);
    }
    if(true == rv)
    {
        simply_thread_queue_data_add(ST_CAST_THREAD(queue), data);
        simply_thread_resume_queues();  //!< Start any Higher priority queues that where waiting
    }
    MUTEX_RELEASE();
    PRINT_MSG("%s Completed\r\n", __FUNCTION__);
    return rv;
}


/**
 * @brief Function that retrieves data from the queue
 * @param queue handle of the queue in question
 * @param data ptr to the object to place the data in
 * @param block_time how long to wait on the queue
 * @return true on success, false otherwise
 */
bool simply_thread_queue_rcv(simply_thread_queue_t queue, void *data, unsigned int block_time)
{
    PRINT_MSG("%s\r\n", __FUNCTION__);
    struct simply_thread_queue_data_s *typed;
    struct simply_thread_task_s *c_task;
    bool rv;

    if(NULL == queue || NULL == data)
    {
        return false;
    }
    typed = ST_CAST_THREAD(queue);
    MUTEX_GET();
    c_task = simply_thread_get_ex_task();
    rv = true;
    if(NULL == c_task)
    {
        block_time = 0;
    }
    if(0 == typed->current_count)
    {
        rv = simply_thread_queue_lockdown(typed, c_task, block_time, SIMPLY_THREAD_QUEUE_WAIT_ADD);
    }
    if(true == rv)
    {
        simply_thread_queue_data_remove(typed, data);
        simply_thread_resume_queues();  //!< Start any Higher priority queues that where waiting
    }
    MUTEX_RELEASE();
    PRINT_MSG("%s Completed\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief Function that resumes tasks waiting on queues
 */
static void simply_thread_resume_queues(void)
{
    bool queues_ready = false;
    struct simply_thread_queue_data_s *queue;
    struct simply_thread_task_s *ready_task;
    assert(true == simply_thread_master_mutex_locked()); //We must be locked
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_queue_data.queue_list); i++)
    {
        // PRINT_MSG("i:%u\r\n", i);
        queue = m_queue_data.queue_list[i];
        ready_task = NULL;
        if(NULL != queue)
        {
            for(unsigned int j = 0; j < ARRAY_MAX_COUNT(queue->wait_list); j++)
            {
                // PRINT_MSG("j:%u\r\n", j);
                if(NULL != queue->wait_list[j].task)
                {
                    if(SIMPLY_THREAD_TASK_BLOCKED == queue->wait_list[j].task->state)
                    {
                        if(SIMPLY_THREAD_QUEUE_WAIT_ADD == queue->wait_list[j].reason)
                        {
                            if(queue->current_count > 0)
                            {
                                if(NULL == ready_task)
                                {
                                    ready_task = queue->wait_list[j].task;
                                    PRINT_MSG("\t%s ready\r\n", ready_task->name);
                                }
                                else if(queue->wait_list[j].task->priority > ready_task->priority)
                                {
                                    ready_task = queue->wait_list[j].task;
                                    PRINT_MSG("\t%s ready\r\n", ready_task->name);
                                }
                            }
                        }
                        else
                        {
                            assert(SIMPLY_THREAD_QUEUE_WAIT_REMOVE == queue->wait_list[j].reason);
                            if(queue->current_count < queue->max_count)
                            {
                                if(NULL == ready_task)
                                {
                                    ready_task = queue->wait_list[j].task;
                                    PRINT_MSG("\t%s ready\r\n", ready_task->name);
                                }
                                else if(queue->wait_list[j].task->priority > ready_task->priority)
                                {
                                    ready_task = queue->wait_list[j].task;
                                    PRINT_MSG("\t%s ready\r\n", ready_task->name);
                                }
                            }
                        }
                    }
                }
            }
            if(NULL != ready_task)
            {
                queues_ready = true;
                PRINT_MSG("\t%s Setting %s to SIMPLY_THREAD_TASK_READY\r\n", __FUNCTION__, ready_task->name);
                assert(ready_task->state == SIMPLY_THREAD_TASK_BLOCKED);
                simply_thread_set_task_state_from_locked(ready_task, SIMPLY_THREAD_TASK_READY);
                assert(true == simply_thread_master_mutex_locked()); //We must be locked
                return;
            }
        }
    }
    if(false == queues_ready)
    {
        PRINT_MSG("\t%s No tasks waiting on queues\r\n", __FUNCTION__);
    }
}
