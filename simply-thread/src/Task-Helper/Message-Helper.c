/*
 * Message-Helper.c
 *
 *  Created on: Mar 6, 2020
 *      Author: kade
 */

#include <Message-Helper.h>
#include <simply-thread-log.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>

#ifndef MAX_MSG_SIZE
#define MAX_MSG_SIZE 100
#endif //MAX_MSG_SIZE

#define MSG_QUEUE_KEY IPC_PRIVATE

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define PRINT_MSG(...) printf(__VA_ARGS__)

struct message_helper_message_s
{
	bool internal;
	uint32_t message_size;
	uint8_t data[MAX_MSG_SIZE];
};

struct formatted_message_s
{
	long type;
	char msg[sizeof(struct message_helper_message_s)];
};

/**
 * @brief print an error message
 * @param error
 */
static inline void print_error_message(int error)
{
	if(0 == error)
	{
		return;
	}
	switch(error)
	{
		case EACCES:
			ST_LOG_ERROR("-----message queue returned EACCES\r\n");
			break;
		case EEXIST:
			ST_LOG_ERROR("-----message queue returned EEXIST\r\n");
			break;
		case ENOENT:
			ST_LOG_ERROR("-----message queue returned ENOENT\r\n");
			break;
		case ENOMEM:
			ST_LOG_ERROR("-----message queue returned ENOMEM\r\n");
			break;
		case ENOSPC:
			ST_LOG_ERROR("-----message queue returned ENOSPC\r\n");
			break;
//		case EAGAIN:
//			ST_LOG_ERROR("-----message queue returned EAGAIN\r\n");
//			break;
		default:
			ST_LOG_ERROR("Error Unknown error %i\r\n", error);
			SS_ASSERT(false);
	}
}

/**
 * Initialize the ipc message queue
 */
static inline int init_msg_queue(void)
{
    int error_number;
    int result;
    result = msgget(MSG_QUEUE_KEY, IPC_CREAT | IPC_EXCL | 0666);
    if(0  > result)
    {
        error_number = errno;
        print_error_message(error_number);
    }
    SS_ASSERT( 0 <= result);
    return result;
}

/**
 * The Modules worker Function
 * @param args
 */
static void * Message_Helper_Local_Worker(void * args)
{
	struct message_helper_message_s result_data;
	struct formatted_message_s raw;
	Message_Helper_Instance_t * typed;
	int result;
	static const int result_size = sizeof(result_data);
	typed = args;
	SS_ASSERT(NULL != typed);
	while( false == typed->Kill_Worker)
	{
		result = msgrcv(typed->QueueId, &raw, sizeof(struct formatted_message_s), 1, 0);
		SS_ASSERT(result >= result_size);
		memcpy(&result_data, raw.msg, sizeof(result_data));
		if(false == result_data.internal)
		{
			typed->cb(result_data.data, result_data.message_size);
		}
	}
	return NULL;
}

/**
 * @brief Create a new message helper
 * @param worker function pointer to the worker task to call when a new message is received
 * @return NULL on error.  Otherwise pointer to the new message helper
 */
Message_Helper_Instance_t * New_Message_Helper(Message_Helper_On_Message worker)
{
	Message_Helper_Instance_t * rv;
	SS_ASSERT(NULL != worker);
	rv = malloc(sizeof(Message_Helper_Instance_t));
	SS_ASSERT(NULL != rv);
	rv->Kill_Worker = false;
	rv->cb = worker;
	rv->QueueId = init_msg_queue();
	SS_ASSERT(0 == pthread_create(&rv->Worker_Thread, NULL, Message_Helper_Local_Worker, rv));
	return rv;
}

/**
 * @brief Function that destroys a message helper
 * @param helper pointer to the message helper to destroy
 */
void Remove_Message_Helper(Message_Helper_Instance_t * helper)
{
	struct message_helper_message_s formatted;
	struct formatted_message_s raw;
	int result;
	SS_ASSERT(NULL != helper);
	helper->Kill_Worker = true;
	formatted.internal = true;
	memcpy(raw.msg, &formatted, sizeof(formatted));
	raw.type = 1;
	result = msgsnd(helper->QueueId, &raw, sizeof(formatted), 0);
	SS_ASSERT(result == 0);
	result = msgsnd(helper->QueueId, &raw, sizeof(formatted), 0);
	SS_ASSERT(result == 0);
	pthread_join(helper->Worker_Thread, NULL);
    if (msgctl(helper->QueueId, IPC_RMID, NULL) == -1)
    {
    	ST_LOG_ERROR("Failed to delete my message queue\r\n");
    }
	free(helper);
}

/**
 * @brief Function that sends a message
 * @param helper
 * @param msg
 * @param message_size
 */
void Message_Helper_Send(Message_Helper_Instance_t * helper, void * msg, uint32_t message_size)
{
	struct message_helper_message_s formatted;
	struct formatted_message_s raw;
	int result;
	int err;
	SS_ASSERT(NULL != helper);
	SS_ASSERT(NULL != msg);
	SS_ASSERT(0 < message_size);
	SS_ASSERT(ARRAY_MAX_COUNT(formatted.data) >= message_size);
	formatted.internal = false;
	formatted.message_size = message_size;
	memcpy(formatted.data, msg, message_size);
	memcpy(raw.msg, &formatted, sizeof(formatted));
	raw.type = 1;
	result = -1;
	while(result != 0)
	{
		result = msgsnd(helper->QueueId, &raw, sizeof(formatted), 0);
		err = errno;
		if(0 != result)
		{
			print_error_message(err);
		}
	}
	SS_ASSERT(result == 0);
}
