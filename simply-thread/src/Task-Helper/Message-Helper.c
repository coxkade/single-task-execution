/*
 * Message-Helper.c
 *
 *  Created on: Mar 6, 2020
 *      Author: kade
 */

#include <Message-Helper.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef MAX_MSG_SIZE
#define MAX_MSG_SIZE 250
#endif //MAX_MSG_SIZE

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
 * Initialize the ipc message queue
 */
static inline int init_msg_queue(void)
{
    int error_number;
    int result;
    result = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    if(0  > result)
    {
        error_number = errno;
        switch(error_number)
        {
            case EACCES:
                PRINT_MSG("-----msgget returned EACCES\r\n");
                break;
            case EEXIST:
                PRINT_MSG("-----msgget returned EEXIST\r\n");
                break;
            case ENOENT:
                PRINT_MSG("-----msgget returned ENOENT\r\n");
                break;
            case ENOMEM:
                PRINT_MSG("-----msgget returned ENOMEM\r\n");
                break;
            case ENOSPC:
                PRINT_MSG("-----msgget returned ENOSPC\r\n");
                break;
            default:
                PRINT_MSG("Error Unknown error\r\n");
                assert(false);
        }
    }
    assert( 0 <= result);
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
	typed = args;
	assert(NULL != typed);
	while( false == typed->Kill_Worker)
	{
		result = msgrcv(typed->QueueId, &raw, sizeof(struct formatted_message_s), 1, 0);
		assert(result == sizeof(struct formatted_message_s));
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
	assert(NULL != worker);
	rv = malloc(sizeof(Message_Helper_Instance_t));
	assert(NULL != rv);
	rv->Kill_Worker = false;
	rv->cb = worker;
	rv->QueueId = init_msg_queue();
	assert(0 == pthread_create(&rv->Worker_Thread, NULL, Message_Helper_Local_Worker, rv));
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
	assert(NULL != helper);
	helper->Kill_Worker = true;
	formatted.internal = true;
	memcpy(raw.msg, &formatted, sizeof(formatted));
	raw.type = 1;
	result = msgsnd(helper->QueueId, &raw, sizeof(formatted), 0);
	assert(result == sizeof(formatted));
	pthread_join(helper->Worker_Thread, NULL);
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
	assert(NULL != helper);
	assert(NULL != msg);
	assert(0 < message_size);
	assert(ARRAY_MAX_COUNT(formatted.data) <= message_size);
	formatted.internal = false;
	formatted.message_size = message_size;
	memcpy(formatted.data, msg, message_size);
	memcpy(raw.msg, &formatted, sizeof(formatted));
	raw.type = 1;
	result = msgsnd(helper->QueueId, &raw, sizeof(formatted), 0);
	assert(result == sizeof(formatted));
}
