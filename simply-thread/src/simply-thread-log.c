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
#include <pthread.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/types.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifndef SIMPLY_THREAD_LOG_BUFFER_SIZE
#define SIMPLY_THREAD_LOG_BUFFER_SIZE 1024
#endif //SIMPLY_THREAD_LOG_BUFFER_SIZE

#define DEBUG_LOG

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
};

struct message_buffer_s
{
    char buffer[SIMPLY_THREAD_LOG_BUFFER_SIZE];
    const char *color;
    bool Finished;
};

struct internal_log_message_data_s
{
	enum
	{
		MSG_TYPE_EXIT,
		MSG_TYPE_PRINT
	}type;
	void * data;
};

struct formatted_message_s
{
    long type;
    char msg[sizeof(struct internal_log_message_data_s)];
};

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
};

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
		msg.data = NULL;
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
static void message_printer(void *message)
{
	char time_buffer[100];
	unsigned int buffer_size;
	time_t t;
	struct tm tm;
    struct message_buffer_s **typed;
    typed = (struct message_buffer_s **)message;

    //Setup the time message string
    t = time(NULL);
    tm = *localtime(&t);
    snprintf(time_buffer, ARRAY_MAX_COUNT(time_buffer), "%d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);

    buffer_size = strlen(time_buffer) + strlen(typed[0]->buffer) + strlen(typed[0]->color) + strlen(COLOR_RESET) + 10;
	printf("%s%s%s%s", typed[0]->color, time_buffer, typed[0]->buffer, COLOR_RESET);
	typed[0]->Finished = true;
}

static void * log_worker(void * passed)
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
			if( MSG_TYPE_PRINT == data.type )
			{
				message_printer(data.data);
			}
			if( MSG_TYPE_EXIT == data.type)
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
    int result;
    result = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    if(result<0)
    {
    	printf("Error %i initializing the log message queue\r\n", errno);
    	assert(0 <= result);
    }
    return result;
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
static inline void send_message(void * data, unsigned int data_size)
{
	struct internal_log_message_data_s msg;
	struct formatted_message_s raw;
	int result;
	init_if_needed();
	msg.type = MSG_TYPE_PRINT;
	msg.data = data;
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
    send_message( &out_buffer, sizeof(struct message_buffer_s *));
    while(false == out_buffer->Finished) {}
}

