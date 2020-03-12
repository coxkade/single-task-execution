/**
 * @file simply-thread-log.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 *
 */

#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <Message-Helper.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifndef SIMPLY_THREAD_LOG_BUFFER_SIZE
#define SIMPLY_THREAD_LOG_BUFFER_SIZE 1024
#endif //SIMPLY_THREAD_LOG_BUFFER_SIZE

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct simp_thread_log_module_data_s
{
    Message_Helper_Instance_t *msg_helper;
    bool initialized;
    pthread_mutex_t mutex;
};

struct message_buffer_s
{
    char buffer[SIMPLY_THREAD_LOG_BUFFER_SIZE];
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct simp_thread_log_module_data_s print_module_data =
{
    .msg_helper = NULL,
    .initialized = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief cleanup on exit
 */
static void ss_log_on_exit(void)
{
    if(NULL != print_module_data.msg_helper)
    {
        Remove_Message_Helper(print_module_data.msg_helper);
    }
}

/**
 * Callback function that prints the actual message
 * @param message
 * @param message_size
 */
static void message_printer(void *message, uint32_t message_size)
{
    struct message_buffer_s **typed;
    typed = message;
    assert(NULL != typed && sizeof(struct message_buffer_s *) == message_size);
    printf("%s", typed[0]->buffer);
    free(typed[0]);
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
            print_module_data.msg_helper = New_Message_Helper(message_printer);
            assert(NULL != print_module_data.msg_helper);
            atexit(ss_log_on_exit);
            print_module_data.initialized = true;
        }
        pthread_mutex_unlock(&print_module_data.mutex);
    }
}

/**
 * @brief Function that  prints a message in color
 * @param fmt Standard printf format
 */
void simply_thread_log(const char *color, const char *fmt, ...)
{
    char final_buffer[SIMPLY_THREAD_LOG_BUFFER_SIZE];
    char time_buffer[100];
    time_t t;
    struct tm tm;
    va_list args;
    unsigned int buffer_size;

    struct message_buffer_s *out_buffer;

    init_if_needed();

    //Setup the time message string
    t = time(NULL);
    tm = *localtime(&t);
    snprintf(time_buffer, ARRAY_MAX_COUNT(time_buffer), "%d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);

    va_start(args, fmt);
    int rc = vsnprintf(final_buffer, ARRAY_MAX_COUNT(final_buffer), fmt, args);
    assert(0 < rc);
    va_end(args);
    buffer_size = strlen(time_buffer) + strlen(final_buffer) + strlen(color) + strlen(COLOR_RESET) + 10;
    out_buffer = malloc(sizeof(struct message_buffer_s));
    assert(NULL != out_buffer);
    snprintf(out_buffer->buffer, buffer_size, "%s%s%s%s", color, time_buffer, final_buffer, COLOR_RESET);
    Message_Helper_Send(print_module_data.msg_helper, &out_buffer, sizeof(struct message_buffer_s *));
}
