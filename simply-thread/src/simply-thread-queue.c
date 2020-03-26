/**
 * @file simply-thread-queue.c
 * @author Kade Cox
 * @date Created: Mar 26, 2020
 * @details
 *
 */

#include <simply-thread-log.h>
#include <simply-thread-queue.h>
#include <simply_thread_system_clock.h>
#include <Message-Helper.h>
#include <TCB.h>
#include <stdlib.h>
#include <string.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_WHITE, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

#ifndef MAX_WAIT_THREADS
#define MAX_WAIT_THREADS 25
#endif //MAX_WAIT_THREADS

#ifndef MAX_NUM_QUEUES
#define MAX_NUM_QUEUES 100
#endif //MAX_NUM_QUEUES

#ifndef MAX_QUEUE_DATA_SIZE
#define MAX_QUEUE_DATA_SIZE 100
#endif //MAX_QUEUE_DATA_SIZE

#ifndef MAX_QUEUE_ELEMENTS
#define MAX_QUEUE_ELEMENTS 100
#endif //MAX_QUEUE_ELEMENTS

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct ss_queue_send_data_s ss_queue_send_data_s; //Forward declaration

struct ss_queue_rcv_data_s ss_queue_rcv_data_s; //Forward declaration

struct queue_wait_task_s
{
    tcb_task_t *waiting_task;  //!< Hold the number of threads waiting on the mutex
    uint64_t count;
    uint64_t max_count;
    sys_clock_on_tick_handle_t tick_handle;
    enum
    {
        SS_QUEUE_FULL,
        SS_QUEUE_EMPTY
    } wait_reason; //!< Details the reason for the wait
    union
    {
        struct ss_queue_send_data_s *send;
        struct ss_queue_rcv_data_s *rcv;
    } wait_data; //!< Data used in the wait logic
}; //!< Holds dataa for a task waiting on a queue

struct queue_message_element_s
{
    bool in_use;
    char buffer[MAX_QUEUE_DATA_SIZE];
}; //!<Single queue element data;

struct single_queue_data_s
{
    struct queue_wait_task_s wait_tasks [MAX_WAIT_THREADS];
    struct queue_message_element_s elements[MAX_QUEUE_ELEMENTS];
    unsigned int element_size;
    unsigned int max_elements;
    bool alocated;
}; //!< Structure for a single queue

struct ss_queue_module_data_s
{
    Message_Helper_Instance_t *m_helper;
    struct single_queue_data_s all_queues[MAX_NUM_QUEUES];
}; //!< Structure that holds all the data local to the queue

struct ss_queue_create_data_s
{
    const char *name;
    unsigned int queue_size;
    unsigned int element_size;
    struct single_queue_data_s *result;
}; //!< Data used to create a queue

struct ss_queue_get_count_data_s
{
    unsigned int count;
    struct single_queue_data_s *queue;
}; //!< Data used in the get count command

struct ss_queue_send_data_s
{
    void *data;
    unsigned int block_time;
    bool result;
    tcb_task_t *task;
    struct single_queue_data_s *queue;
} ss_queue_send_data_s; //!< Data used in a queue send

struct ss_queue_rcv_data_s
{
    bool result;
    struct single_queue_data_s *queue;
    tcb_task_t *task;
    void *data;
    unsigned int block_time;
} ss_queue_rcv_data_s; //!< Data used in a queue rcv

struct ss_queue_msg_s
{
    enum
    {
        SS_QUEUE_CREATE,
        SS_QUEUE_COUNT,
        SS_QUEUE_SEND,
        SS_QUEUE_RCV,
        SS_QUEUE_TIMEOUT
    } type;
    union
    {
        struct ss_queue_create_data_s *create;
        struct ss_queue_get_count_data_s *count;
        struct ss_queue_send_data_s *send;
        struct ss_queue_rcv_data_s *rcv;
        struct queue_wait_task_s *time_out;
    } data;
    bool *finished;
}; //!< Structures to use with the queue helpers worker message queue

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct ss_queue_module_data_s queue_data =
{
    .m_helper = NULL
}; //!< variable holding the local module data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * Function that gets the number of used queue elements
 * @param queue ptr to the queue to use
 * @return the number of used queue elements
 */
static unsigned int queue_used_elements(struct single_queue_data_s *queue)
{
    unsigned int rv = 0;
    SS_ASSERT(NULL != queue);
    for(int i = 0; i < ARRAY_MAX_COUNT(queue->elements); i++)
    {
        if(true == queue->elements[i].in_use)
        {
            rv++;
        }
    }
    return rv;
}

/**
 * Function that tidies a queue
 * @param queue
 */
static void ss_queue_tidy(struct single_queue_data_s *queue)
{
    for(int i = 0; i < (ARRAY_MAX_COUNT(queue->elements) - 1); i++)
    {
        if(false ==  queue->elements[i].in_use && true == queue->elements[i + 1].in_use)
        {
            //we need to swap the data
            memcpy(queue->elements[i].buffer, queue->elements[i + 1].buffer, ARRAY_MAX_COUNT(queue->elements[i].buffer));
            queue->elements[i].in_use = queue->elements[i + 1].in_use;
            queue->elements[i + 1].in_use = false;
        }
    }
}

/**
 * @brief pop data into a buffer
 * @param queue
 * @param data
 */
static void ss_queue_pop(struct single_queue_data_s *queue, struct ss_queue_rcv_data_s *data)
{
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    SS_ASSERT(true == queue->elements[0].in_use);
    SS_ASSERT(NULL != data->data);
    memcpy(data->data, queue->elements[0].buffer, ARRAY_MAX_COUNT(queue->elements[0].buffer));
    data->result = true;
    queue->elements[0].in_use = false;
    ss_queue_tidy(queue);
    ss_queue_tidy(queue);
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief push some data onto the queue
 * @param queue
 * @param data
 */
static void ss_queue_push(struct single_queue_data_s *queue, struct ss_queue_send_data_s *data)
{
    bool added;
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    added = false;
    SS_ASSERT(data->queue == queue);
    for(int i = 0; i < ARRAY_MAX_COUNT(queue->elements) && false == added; i++)
    {
        if(false == queue->elements[i].in_use)
        {
            queue->elements[i].in_use = true;
            memcpy(queue->elements[i].buffer, data->data, ARRAY_MAX_COUNT(queue->elements[i].buffer));
            data->result = true;
            added = true;
        }
    }
    SS_ASSERT(true == added);
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Function that the best task that is waiting on a receive
 * @param queue
 * @return true if a task was resumed
 */
static bool resume_waiting_rcv(struct single_queue_data_s *queue)
{
    unsigned int c_count;
    tcb_task_t *best_task;
    unsigned int used_index;

    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);

    bool rv = false;
    best_task = NULL;
    c_count = queue_used_elements(queue);
    if(0 != c_count)
    {
        for(int i = 0; i < ARRAY_MAX_COUNT(queue->wait_tasks); i++)
        {
            if(NULL != queue->wait_tasks[i].waiting_task && SS_QUEUE_EMPTY == queue->wait_tasks[i].wait_reason)
            {
                if(NULL == best_task)
                {
                    used_index = i;
                    best_task = queue->wait_tasks[i].waiting_task;
                }
                else if(best_task->priority < queue->wait_tasks[i].waiting_task->priority)
                {
                    used_index = i;
                    best_task = queue->wait_tasks[i].waiting_task;
                }
            }
        }
    }
    if(NULL != best_task)
    {
        //Set the task state to ready
        ss_queue_pop(queue, queue->wait_tasks[used_index].wait_data.rcv);
        simply_thead_system_clock_deregister_on_tick(queue->wait_tasks[used_index].tick_handle);
        tcb_set_task_state(SIMPLY_THREAD_TASK_READY, best_task);
        queue->wait_tasks[used_index].waiting_task = NULL;
        rv = true;
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * Resume the task waiting to send with the highest priority if possible
 * @param queue
 * @return
 */
static bool resume_waiting_send(struct single_queue_data_s *queue)
{
    unsigned int c_count;
    tcb_task_t *best_task;
    unsigned int used_index;
    bool rv;

    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);

    rv = false;
    best_task = NULL;
    c_count = queue_used_elements(queue);

    if(c_count < queue->max_elements)
    {
        //We can add an element to the queue
        for(int i = 0; i < ARRAY_MAX_COUNT(queue->wait_tasks); i++)
        {
            if(NULL != queue->wait_tasks[i].waiting_task && SS_QUEUE_FULL == queue->wait_tasks[i].wait_reason)
            {
                if(NULL == best_task)
                {
                    used_index = i;
                    best_task = queue->wait_tasks[i].waiting_task;
                }
                else if(best_task->priority < queue->wait_tasks[i].waiting_task->priority)
                {
                    used_index = i;
                    best_task = queue->wait_tasks[i].waiting_task;
                }
            }
        }
    }
    if(NULL != best_task)
    {
        //Set the task state to ready
        ss_queue_push(queue, queue->wait_tasks[used_index].wait_data.send);
        simply_thead_system_clock_deregister_on_tick(queue->wait_tasks[used_index].tick_handle);
        tcb_set_task_state(SIMPLY_THREAD_TASK_READY, best_task);
        queue->wait_tasks[used_index].waiting_task = NULL;
        rv = true;
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief resume any tasks that can be
 * @param queue
 */
static inline void resume_queue_tasks(struct single_queue_data_s *queue)
{
    while(true == resume_waiting_rcv(queue)) {}
    while(true == resume_waiting_send(queue)) {}
}

/**
 * on tick handler for queue operations
 * @param handle
 * @param tickval
 * @param args
 */
static void ss_queue_on_tick(sys_clock_on_tick_handle_t handle, uint64_t tickval, void *args)
{
    bool comp;
    struct ss_queue_msg_s message;
    struct queue_wait_task_s *typed;
    typed = args;

    SS_ASSERT(NULL != typed);
    if(NULL != typed->waiting_task && handle == typed->tick_handle)
    {
        SS_ASSERT(handle == typed->tick_handle);
        typed->count++;
        if(typed->count >= typed->max_count)
        {
            //Ok we need to handle the timeout
            comp = false;
            message.type = SS_QUEUE_TIMEOUT;
            message.data.time_out = typed;
            message.finished = &comp;
            Message_Helper_Send(queue_data.m_helper, &message, sizeof(message));
            while(false == comp) {}
        }
    }
}

/**
 * @brief handle the on tick message type
 * @param data
 */
static void handle_queue_on_tick(struct queue_wait_task_s *data)
{
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    if(NULL != data)
    {
        if(NULL != data->waiting_task)
        {
            simply_thead_system_clock_deregister_on_tick(data->tick_handle);
            if(SS_QUEUE_FULL == data->wait_reason)
            {
                data->wait_data.send->result = false;
            }
            else
            {
                data->wait_data.rcv->result = false;
            }

            tcb_set_task_state(SIMPLY_THREAD_TASK_READY, data->waiting_task);
            data->waiting_task = NULL;
        }
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Function that allocates a new queue
 * @param data
 */
static inline void handle_queue_create(struct ss_queue_create_data_s *data)
{
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    data->result = NULL;
    if(NULL != data->name && ARRAY_MAX_COUNT(queue_data.all_queues[0].elements[0].buffer) >= data->element_size && 0 < data->queue_size &&
            0 < data->element_size)
    {
        for(int i = 0; i < ARRAY_MAX_COUNT(queue_data.all_queues) && NULL == data->result; i++)
        {
            if(false == queue_data.all_queues[i].alocated && ARRAY_MAX_COUNT(queue_data.all_queues[i].elements[0].buffer) >= data->element_size)
            {
                queue_data.all_queues[i].alocated = true;
                data->result = &queue_data.all_queues[i];
                if(ARRAY_MAX_COUNT(data->result->elements) < data->queue_size)
                {
                    ST_LOG_ERROR("Warning Queue too large allocating queue of size %u instead of %u\r\n", ARRAY_MAX_COUNT(data->result->elements), data->queue_size);
                    data->queue_size = ARRAY_MAX_COUNT(data->result->elements);
                }

                data->result->element_size = data->element_size;
                data->result->max_elements = data->queue_size;
                for(int j = 0; j < ARRAY_MAX_COUNT(data->result->wait_tasks); j++)
                {
                    data->result->wait_tasks[j].waiting_task = NULL;
                }
                for(int j = 0; j < ARRAY_MAX_COUNT(data->result->elements); j++)
                {
                    data->result->elements[j].in_use = false;
                }
                SS_ASSERT(true == data->result->alocated);
            }
        }
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Function that handles the queue count command
 * @param data
 */
static inline void handle_queue_count(struct ss_queue_get_count_data_s *data)
{
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    data->count = queue_used_elements(data->queue);
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Handle the queue send
 * @param data
 */
static inline void handle_queue_send(struct ss_queue_send_data_s *data)
{
    bool wait_started;
    unsigned int c_count;

    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    data->result = false;
    if(NULL != data->queue && NULL != data->data)
    {
        c_count = queue_used_elements(data->queue);
        if(NULL == data->task && 0 < data->block_time)
        {
            PRINT_MSG("\t\tblock_time overwritten\r\n");
            data->block_time = 0;
        }
        SS_ASSERT(c_count <= data->queue->max_elements);

        if(c_count == data->queue->max_elements)
        {
            PRINT_MSG("\t\tWait for send required\r\n");
            if(0 != data->block_time)
            {
                //The Queue is full and wait time is non zero
                wait_started = false;
                SS_ASSERT(NULL != data->task);
                for(int i = 0; i < ARRAY_MAX_COUNT(data->queue->wait_tasks) && false == wait_started; i++)
                {
                    if(NULL == data->queue->wait_tasks[i].waiting_task)
                    {
                        wait_started = true;
                        data->queue->wait_tasks[i].waiting_task = data->task;
                        data->queue->wait_tasks[i].count = 0;
                        data->queue->wait_tasks[i].max_count = data->block_time;
                        data->queue->wait_tasks[i].wait_data.send = data;
                        data->queue->wait_tasks[i].wait_reason = SS_QUEUE_FULL;
                        PRINT_MSG("\t\tSetting %s to SIMPLY_THREAD_TASK_BLOCKED\r\n", data->queue->wait_tasks[i].waiting_task->name);
                        tcb_set_task_state(SIMPLY_THREAD_TASK_BLOCKED, data->queue->wait_tasks[i].waiting_task);
                        data->queue->wait_tasks[i].tick_handle = simply_thead_system_clock_register_on_tick(ss_queue_on_tick, &data->queue->wait_tasks[i]);
                        //We do not set the result at this point as there may have been a timeout
                    }
                }
            }
        }
        else
        {
            //The Queue is not full
            ss_queue_push(data->queue, data); //This will set the result to true
            resume_queue_tasks(data->queue);
        }
    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Function for handling queue receive
 * @param data
 */
static void handle_queue_rcv(struct ss_queue_rcv_data_s *data)
{
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    bool wait_started;
    unsigned int c_count;

    data->result = false;
    if(NULL != data->queue && NULL != data->data)
    {
        c_count = queue_used_elements(data->queue);
        if(NULL == data->task && 0 < data->block_time)
        {
            data->block_time = 0;
        }
        SS_ASSERT(c_count <= data->queue->max_elements);

        if(c_count == 0)
        {
            if(0 != data->block_time)
            {
                //Queue is empty.  We need to wait for data
                wait_started = false;
                SS_ASSERT(NULL != data->task);
                for(int i = 0; i < ARRAY_MAX_COUNT(data->queue->wait_tasks) && false == wait_started; i++)
                {
                    if(NULL == data->queue->wait_tasks[i].waiting_task)
                    {
                        wait_started = true;
                        data->queue->wait_tasks[i].waiting_task = data->task;
                        data->queue->wait_tasks[i].count = 0;
                        data->queue->wait_tasks[i].max_count = data->block_time;
                        data->queue->wait_tasks[i].wait_data.rcv = data;
                        data->queue->wait_tasks[i].wait_reason = SS_QUEUE_EMPTY;
                        tcb_set_task_state(SIMPLY_THREAD_TASK_BLOCKED, data->queue->wait_tasks[i].waiting_task);
                        data->queue->wait_tasks[i].tick_handle = simply_thead_system_clock_register_on_tick(ss_queue_on_tick, &data->queue->wait_tasks[i]);
                        //We do not set the result at this point as there may have been a timeout
                    }
                }
            }
        }
        else
        {
            ss_queue_pop(data->queue, data); //pop will set result to true
            resume_queue_tasks(data->queue);
        }

    }
    PRINT_MSG("\t%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief function that handles queue work
 * @param message
 * @param message_size
 */
static void ss_queue_on_message(void *message, uint32_t message_size)
{
    struct ss_queue_msg_s *typed;
    typed = message;
    SS_ASSERT(NULL != typed && sizeof(struct ss_queue_msg_s) == message_size);
    switch(typed->type)
    {
        case SS_QUEUE_CREATE:
            handle_queue_create(typed->data.create);
            typed->finished[0] = true;
            break;
        case SS_QUEUE_COUNT:
            handle_queue_count(typed->data.count);
            typed->finished[0] = true;
            break;
        case SS_QUEUE_SEND:
            handle_queue_send(typed->data.send);
            typed->finished[0] = true;
            break;
        case SS_QUEUE_RCV:
            handle_queue_rcv(typed->data.rcv);
            typed->finished[0] = true;
            break;
        case SS_QUEUE_TIMEOUT:
            handle_queue_on_tick(typed->data.time_out);
            typed->finished[0] = true;
            break;
        default:
            SS_ASSERT(true == false);
            break;
    }
}

/**
 * Initialize the module in the TCB context
 * @param data
 */
static void ss_queue_init_in_tcb(void *data)
{
    SS_ASSERT(NULL == data);
    if(NULL == queue_data.m_helper)
    {
        for(int i = 0; i < ARRAY_MAX_COUNT(queue_data.all_queues); i++)
        {
            queue_data.all_queues[i].alocated = false;
        }
        queue_data.m_helper = New_Message_Helper(ss_queue_on_message, "SS_QUEUE_MESSAGE_HANDLER");
        SS_ASSERT(NULL != queue_data.m_helper);
    }
}

/**
 * Initialize this queue if required
 */
static void ss_queue_init_if_needed(void)
{
    if(NULL == queue_data.m_helper)
    {
        run_in_tcb_context(ss_queue_init_in_tcb, NULL);
    }
}

/**
 * @brief Function that cleans up the simply thread queue
 */
void simply_thread_queue_cleanup(void)
{
    queue_data.m_helper = NULL;
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
    struct ss_queue_create_data_s data;
    struct ss_queue_msg_s message;
    bool comp;

    PRINT_MSG("%s Running\r\n", __FUNCTION__);

    ss_queue_init_if_needed();

    comp = false;
    data.element_size = element_size;
    data.queue_size = queue_size;
    data.name = name;
    message.type = SS_QUEUE_CREATE;
    message.data.create = &data;
    message.finished = &comp;

    Message_Helper_Send(queue_data.m_helper, &message, sizeof(message));
    while(false == comp) {}
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return data.result;
}

/**
 * @brief Function that gets the current number of elements on the queue
 * @param queue handle of the queue in question
 * @return 0xFFFFFFFF on error. Otherwise the queue count
 */
unsigned int simply_thread_queue_get_count(simply_thread_queue_t queue)
{
    struct ss_queue_get_count_data_s data;
    struct ss_queue_msg_s message;
    bool comp;

    PRINT_MSG("%s Running\r\n", __FUNCTION__);

    ss_queue_init_if_needed();
    comp = false;
    data.queue = queue;
    message.type = SS_QUEUE_COUNT;
    message.data.count = &data;
    message.finished = &comp;

    Message_Helper_Send(queue_data.m_helper, &message, sizeof(message));
    while(false == comp) {}
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return data.count;
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
    struct ss_queue_send_data_s worker;
    struct ss_queue_msg_s message;
    bool comp;

    PRINT_MSG("%s Running\r\n", __FUNCTION__);

    ss_queue_init_if_needed();
    comp = false;
    worker.task = tcb_task_self();
    worker.block_time = block_time;
    worker.data = data;
    worker.queue = queue;
    message.type = SS_QUEUE_SEND;
    message.finished = &comp;
    message.data.send = &worker;

    Message_Helper_Send(queue_data.m_helper, &message, sizeof(message));
    while(false == comp) {}
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return worker.result;
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
    struct ss_queue_rcv_data_s worker;
    struct ss_queue_msg_s message;
    bool comp;

    PRINT_MSG("%s Running\r\n", __FUNCTION__);

    ss_queue_init_if_needed();
    comp = false;
    worker.block_time = block_time;
    worker.task = tcb_task_self();
    worker.queue = queue;
    worker.data = data;
    message.type = SS_QUEUE_RCV;
    message.data.rcv = &worker;
    message.finished = &comp;


    Message_Helper_Send(queue_data.m_helper, &message, sizeof(message));
    while(false == comp) {}
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return worker.result;
}
