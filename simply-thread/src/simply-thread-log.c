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
    bool cleaned_up;
};

struct message_buffer_s
{
    char buffer[SIMPLY_THREAD_LOG_BUFFER_SIZE];
    const char *color;
    bool Finished;
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
    .mutex = PTHREAD_MUTEX_INITIALIZER,
	.cleaned_up = false
};

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief cleanup on exit
 */
void ss_log_on_exit(void)
{
	print_module_data.cleaned_up = true;
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
	char time_buffer[100];
	unsigned int buffer_size;
	time_t t;
	struct tm tm;
    struct message_buffer_s **typed;
    typed = message;
    assert(NULL != typed && sizeof(struct message_buffer_s *) == message_size);

    //Setup the time message string
    t = time(NULL);
    tm = *localtime(&t);
    snprintf(time_buffer, ARRAY_MAX_COUNT(time_buffer), "%d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);

    buffer_size = strlen(time_buffer) + strlen(typed[0]->buffer) + strlen(typed[0]->color) + strlen(COLOR_RESET) + 10;
	printf("%s%s%s%s", typed[0]->color, time_buffer, typed[0]->buffer, COLOR_RESET);
	typed[0]->Finished = true;
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
    va_list args;
    struct message_buffer_s buffer;
    struct message_buffer_s *out_buffer;
    out_buffer = &buffer;

    init_if_needed();
    assert(NULL != out_buffer);
    va_start(args, fmt);
    int rc = vsnprintf(out_buffer->buffer, ARRAY_MAX_COUNT(out_buffer->buffer), fmt, args);
    assert(0 < rc);
    va_end(args);
    out_buffer->color = color;
    out_buffer->Finished = false;
    Message_Helper_Send(print_module_data.msg_helper, &out_buffer, sizeof(struct message_buffer_s *));
    while(false == out_buffer->Finished) {}
}

