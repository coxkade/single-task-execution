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

//#define DEBUG_MESAGE_HELPER

#ifdef DEBUG_MESAGE_HELPER
#define PRINT_MSG(...) simply_thread_log(COLOR_BLUE,__VA_ARGS__)
//#define PRINT_RAW_MSG(M) print_raw_message(M)
#else
#define PRINT_MSG(...)
//#define PRINT_RAW_MSG(M)
#endif //DEBUG_MESAGE_HELPER


struct message_helper_recv_data_s
{
	Message_Helper_Instance_t * helper;
	bool rcvd;
};

struct message_helper_message_s
{
    bool internal;
    struct message_helper_recv_data_s *rec_data;
    uint32_t message_size;
    uint8_t data[MAX_MSG_SIZE];
};

struct formatted_message_s
{
    long type;
    union{
    char msg[sizeof(struct message_helper_message_s)];
    struct message_helper_message_s decoded;
    };
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
        case EINTR:
        	PRINT_MSG("-----message queue returned EINTR\r\n");
        	break;
        case EINVAL:
        	PRINT_MSG("-----message queue returned EINVAL\r\n");
        	break;
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
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    result = msgget(MSG_QUEUE_KEY, IPC_CREAT | IPC_EXCL | 0666);
    if(0  > result)
    {
        error_number = errno;
        print_error_message(error_number);
    }
    SS_ASSERT(0 <= result);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return result;
}

/**
 * The Modules worker Function
 * @param args
 */
static void *Message_Helper_Local_Worker(void *args)
{
    struct formatted_message_s raw;
    Message_Helper_Instance_t *typed;
    int result;
    static const int result_size = sizeof(raw.decoded);
    typed = args;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != typed);
    while(false == typed->Kill_Worker)
    {
        result = msgrcv(typed->QueueId, &raw, sizeof(struct formatted_message_s), 1, 0);
        typed->processing = true;
        PRINT_MSG("\t%s received message %i %i\r\n", __FUNCTION__, result, errno);

        if(result != -1)
        {
			SS_ASSERT(raw.type == 1);
			SS_ASSERT(result >= result_size);

			if(false == raw.decoded.internal)
			{
				PRINT_MSG("\t%s received external message\r\n", __FUNCTION__);
				raw.decoded.rec_data->rcvd = true;
				typed->cb(raw.decoded.data, raw.decoded.message_size);
				PRINT_MSG("\t%s Finished processing external command\r\n", __FUNCTION__);
			}
			else
			{
				PRINT_MSG("\t%s received internal message\r\n", __FUNCTION__);
				typed->Kill_Worker = true;
			}
        }
        typed->processing = false;
    }
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    if(msgctl(typed->QueueId, IPC_RMID, NULL) == -1)
	{
		ST_LOG_ERROR("Failed to delete my message queue\r\n");
	}
    typed->QueueId = -1;
    return NULL;
}

/**
 * @brief Create a new message helper
 * @param worker function pointer to the worker task to call when a new message is received
 * @return NULL on error.  Otherwise pointer to the new message helper
 */
Message_Helper_Instance_t *New_Message_Helper(Message_Helper_On_Message worker, const char * name)
{
    Message_Helper_Instance_t *rv;
    int result;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != worker);
    rv = malloc(sizeof(Message_Helper_Instance_t));
    SS_ASSERT(NULL != rv);
    rv->Kill_Worker = false;
    rv->cb = worker;
    rv->QueueId = init_msg_queue();
    rv->remove_in_progress = false;
    rv->processing = false;
    rv->clearing = false;
    rv->name = name;
    result = pthread_create(&rv->Worker_Thread, NULL, Message_Helper_Local_Worker, rv);
    SS_ASSERT(0 == result);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return rv;
}

/**
 * @brief Function that clears all messages off of the message buffer
 * @param helper
 */
static void clear_queue(Message_Helper_Instance_t *helper)
{
	bool finished = false;
	struct formatted_message_s raw;
	int result;
	int errorval;

	printf("Clearing the message queue %s\r\n", helper->name);
	SS_ASSERT(false == helper->clearing);
	helper->clearing = true;
	while(false == finished)
	{
		result = msgrcv(helper->QueueId, &raw, sizeof(struct formatted_message_s), 1, IPC_NOWAIT);
		errorval = errno;
		if(0 > result)
		{
			if(ENOMSG == errorval)
			{
				finished = true;
			}
		}
	}
	helper->clearing = false;
}

/**
 * Function that fetches the number of messages on the queue
 * @param helper
 * @return The number of pending queue messages
 */
static msgqnum_t get_msg_count(Message_Helper_Instance_t *helper)
{
	struct msqid_ds message_data;
	int result;
	result = msgctl(helper->QueueId, IPC_STAT, &message_data);
	SS_ASSERT(0 == result);
	return message_data.msg_qnum;
}

static void clear_queue_and_send_kill_command(Message_Helper_Instance_t *helper)
{
	struct formatted_message_s raw;
	msgqnum_t messages_pending;
	int result = -1;
	do{
		clear_queue(helper); //Clear the Queue
		messages_pending = get_msg_count(helper);
		if(0 != messages_pending)
		{
			printf("Queue still has %u messages\r\n", (unsigned int)messages_pending);
		}
	}while(0 != messages_pending);

	SS_ASSERT(0 == messages_pending);
//    helper->Kill_Worker = true;
    raw.decoded.internal = true;
    raw.decoded.message_size = 0;
    memset(raw.decoded.data, 0,  ARRAY_MAX_COUNT(raw.decoded.data));
    raw.type = 1;
    while(0 > result)
    {
		PRINT_MSG("\t%s Sending the kill command for %p\r\n", __FUNCTION__, helper);
		result = msgsnd(helper->QueueId, &raw, sizeof(raw.decoded), 0);
		PRINT_MSG("\t%s send result: %i\r\n", __FUNCTION__, result);
		if(0 > result)
		{
			int eval = errno;
			PRINT_MSG("Error Value: %i\r\n", eval);
			SS_ASSERT(EAGAIN == eval || EIDRM == eval || EINVAL == eval || EINTR == eval);
		}
    }
	while(true == helper->processing) {}
}

/**
 * @brief Function that destroys a message helper
 * @param helper pointer to the message helper to destroy
 */
void Remove_Message_Helper(Message_Helper_Instance_t *helper)
{
//	struct msqid_ds message_data;
//    struct formatted_message_s raw;
//    int result;
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    SS_ASSERT(NULL != helper);
    if(true == helper->remove_in_progress)
    {
    	PRINT_MSG("\t%s Removal already in progress for %p\r\n", __FUNCTION__, helper);
        while(true == helper->remove_in_progress)
        {
        }
        return;
    }

    helper->remove_in_progress = true;
    PRINT_MSG("\t%s Removing %p\r\n", __FUNCTION__, helper);
//    helper->Kill_Worker = true;
//    raw.decoded.internal = true;
//    raw.decoded.message_size = 0;
//    memset(raw.decoded.data, 0,  ARRAY_MAX_COUNT(raw.decoded.data));
//    raw.type = 1;

    while(true == helper->processing) {}

    clear_queue_and_send_kill_command(helper);
//    PRINT_MSG("\t%s Sending the kill command for %p\r\n", __FUNCTION__, helper);
//	result = msgsnd(helper->QueueId, &raw, sizeof(raw.decoded), 0);
//	PRINT_MSG("\t%s send result: %i\r\n", __FUNCTION__, result);
//	if(0 > result)
//	{
//		int eval = errno;
//		PRINT_MSG("Error Value: %i\r\n", eval);
//		SS_ASSERT(EAGAIN == eval || EIDRM == eval || EINVAL == eval || EINTR == eval);
//	}
//
//	while(true == helper->processing) {}
//    PRINT_MSG("\t%sDestroying the Queue for %p\r\n", __FUNCTION__, helper);
//    if(msgctl(helper->QueueId, IPC_RMID, NULL) == -1)
//    {
//        ST_LOG_ERROR("Failed to delete my message queue %i\r\n", errno);
//    }

    PRINT_MSG("\t%s Waiting on worker task to exit\r\n", __FUNCTION__);
    pthread_join(helper->Worker_Thread, NULL);
    helper->Worker_Thread = NULL;
    SS_ASSERT(true == helper->Kill_Worker);
//    if(msgctl(helper->QueueId, IPC_RMID, NULL) == -1)
//    {
//        ST_LOG_ERROR("Failed to delete my message queue\r\n");
//    }
    helper->remove_in_progress = false;
    free(helper);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}


/**
 * @brief Function that sends a message
 * @param helper
 * @param msg
 * @param message_size
 */
void Message_Helper_Send(Message_Helper_Instance_t *helper, void *msg, uint32_t message_size)
{
    struct formatted_message_s raw;
    int result;
    int err;
//    SS_ASSERT(NULL != helper);
    PRINT_MSG("%s Started\r\n", __FUNCTION__);
    while(NULL == helper) {}
    SS_ASSERT(NULL != msg);
    SS_ASSERT(0 < message_size);
    SS_ASSERT(ARRAY_MAX_COUNT(raw.decoded.data) >= message_size);
    raw.decoded.internal = false;
    raw.decoded.rec_data = malloc(sizeof(struct message_helper_recv_data_s));
    SS_ASSERT(NULL != raw.decoded.rec_data);
    raw.decoded.rec_data->helper = helper;
    raw.decoded.rec_data->rcvd = false;
    raw.decoded.message_size = message_size;
    memset(raw.decoded.data, 0,  ARRAY_MAX_COUNT(raw.decoded.data));
    memcpy(raw.decoded.data, msg, message_size);
    raw.type = 1;
    result = -1;
    while(result != 0)
    {
    	PRINT_MSG("\t%s %p sending message %p %u\r\n", __FUNCTION__, helper, msg, message_size);
//    	PRINT_RAW_MSG(&raw);
    	if(true == helper->remove_in_progress)
    	{
    		PRINT_MSG("\t%s Remove in process waiting\r\n", __FUNCTION__);
    		while(true == helper->remove_in_progress) {}
    	}
        result = msgsnd(helper->QueueId, &raw, sizeof(raw.decoded), 0);
        err = errno;
        if(0 != result)
        {
        	if(EINVAL == err)
        	{
        		return;
        	}
        	print_error_message(err);
        }
    }
    SS_ASSERT(result == 0);
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Get the number of pending messages on the message helper
 * @param helper
 * @return The number of pending messages
 */
unsigned int Message_Helper_Pending_Messages(Message_Helper_Instance_t *helper)
{
	SS_ASSERT(NULL != helper);
	return get_msg_count(helper);
}
