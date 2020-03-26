/**
 * @file simply-thread-log.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 *
 */

#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <que-creator.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifndef SIMPLY_THREAD_LOG_BUFFER_SIZE
#define SIMPLY_THREAD_LOG_BUFFER_SIZE 1024
#endif //SIMPLY_THREAD_LOG_BUFFER_SIZE

#ifdef DEBUG_LOG
#define PRINT_MSG(...) printf(__VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_LOG

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct simp_thread_log_module_data_s
{
    pthread_t message_thread;
    int QueueId;
    bool initialized;
    pthread_mutex_t mutex;
    bool cleaned_up;
    bool kill_worker;
}; //! Structure for the local module data

struct message_buffer_s
{
    char buffer[SIMPLY_THREAD_LOG_BUFFER_SIZE];
    const char *color;
}; //!< message buffer struct

struct actual_log_message_s
{
    struct message_buffer_s *data_ptr;
}; //!< Log message

struct internal_log_message_data_s
{
    enum
    {
        MSG_TYPE_EXIT,
        MSG_TYPE_PRINT
    } type;
    struct actual_log_message_s msg;
}; //!< internal log message type

struct formatted_message_s
{
    long type;
    char msg[sizeof(struct internal_log_message_data_s)];
}; //!< Raw internal message

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct simp_thread_log_module_data_s print_module_data =
{
    .initialized = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cleaned_up = false,
    .QueueId = -1
}; //!< The local module data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief cleanup on exit
 */
void ss_log_on_exit(void)
{
    struct internal_log_message_data_s msg;
    struct formatted_message_s raw;
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    pthread_mutex_trylock(&print_module_data.mutex);
    if(true == print_module_data.initialized)
    {
        print_module_data.kill_worker = true;
        msg.type = MSG_TYPE_EXIT;
        memcpy(raw.msg, &msg, sizeof(msg));
        raw.type = 1;
        assert(0 <=  msgsnd(print_module_data.QueueId, &raw, sizeof(msg), 0));
        pthread_join(print_module_data.message_thread, NULL);
        assert(0 == msgctl(print_module_data.QueueId, IPC_RMID, NULL));
    }
    pthread_mutex_unlock(&print_module_data.mutex);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * Callback function that prints the actual message
 * @param message
 * @param message_size
 */
static void message_printer(struct message_buffer_s *message)
{
    char time_buffer[100];
    time_t t;
    struct tm tm;

    //Setup the time message string
    t = time(NULL);
    tm = *localtime(&t);
    snprintf(time_buffer, ARRAY_MAX_COUNT(time_buffer), "%d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);

    printf("%s%s%s%s", message->color, time_buffer, message->buffer, COLOR_RESET);
    free(message);
}

/**
 * @brief worker function that writes out the data
 * @param passed
 */
static void *log_worker(void *passed)
{
    struct formatted_message_s raw;
    struct internal_log_message_data_s data;
    int result;
    while(false == print_module_data.kill_worker)
    {
        result = msgrcv(print_module_data.QueueId, &raw, sizeof(struct formatted_message_s), 1, 0);
        if(result >= sizeof(data))
        {
            memcpy(&data, raw.msg, sizeof(data));
            if(MSG_TYPE_PRINT == data.type)
            {
                message_printer(data.msg.data_ptr);
            }
            if(MSG_TYPE_EXIT == data.type)
            {
                print_module_data.kill_worker = true;
            }
        }
    }
    return NULL;
}

/**
 * Initialize the ipc message queue
 */
static inline int init_msg_queue(void)
{
    return create_new_queue();
}

/**
 * @brief initialize the module if needed
 */
static void init_if_needed(void)
{
    if(false == print_module_data.initialized)
    {
        assert(0 == pthread_mutex_lock(&print_module_data.mutex));
        if(false == print_module_data.initialized)
        {
            print_module_data.kill_worker = false;
            print_module_data.QueueId = init_msg_queue();
            assert(0 == pthread_create(&print_module_data.message_thread, NULL, log_worker, NULL));
            print_module_data.initialized = true;
        }
        pthread_mutex_unlock(&print_module_data.mutex);
    }
}

/**
 * @brief Function that sends a message
 * @param data
 * @param data_size
 */
static inline void send_message(struct message_buffer_s *buffer)
{
    struct internal_log_message_data_s msg;
    struct formatted_message_s raw;
    int result;
    init_if_needed();

    msg.type = MSG_TYPE_PRINT;
    msg.msg.data_ptr = buffer;
    assert(NULL != msg.msg.data_ptr);
    memcpy(raw.msg, &msg, sizeof(msg));
    raw.type = 1;
    result = msgsnd(print_module_data.QueueId, &raw, sizeof(msg), 0);
    if(0 > result)
    {
        PRINT_MSG("result: %i\r\n", result);
    }
    assert(0 <=  result);
}

/**
 * @brief Function that  prints a message in color
 * @param fmt Standard printf format
 */
void simply_thread_log(const char *color, const char *fmt, ...)
{
    va_list args;
    struct message_buffer_s *out_buffer;
    out_buffer = malloc(sizeof(struct message_buffer_s));
    assert(NULL != out_buffer);
    init_if_needed();
    assert(NULL != out_buffer);
    va_start(args, fmt);
    int rc = vsnprintf(out_buffer->buffer, ARRAY_MAX_COUNT(out_buffer->buffer), fmt, args);
    assert(0 < rc);
    va_end(args);
    out_buffer->color = color;
    send_message(out_buffer);
}

