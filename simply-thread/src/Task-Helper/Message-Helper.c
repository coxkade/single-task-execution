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
#include <que-creator.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

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

#ifndef MAX_MESSAGE_HELPERS
#define MAX_MESSAGE_HELPERS 60
#endif //MAX_MESSAGE_HELPERS


/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct message_helper_module_data_s
{
	helper_thread_t * module_helper; //!<The thread used to guarantee thread safety
	int QId; //!< The Id of the message queue
	struct{
		Message_Helper_Instance_t instance;
		bool in_use;
	}reg[MAX_MESSAGE_HELPERS];
};

struct message_helper_internal_message_data_s
{
    enum
    {
        MH_CREATE,
        MH_DESTROY,
    } action; //!< Enum that tells us what to do
    union{
    	struct
		{
    		Message_Helper_On_Message worker;
			const char * name;
			Message_Helper_Instance_t ** result;
		}create; //!< Variables used with the create message
		struct
		{
			Message_Helper_Instance_t * instance;
		}destroy; //!< Variables used with the destroy message
    }msg_data; //!< Union that holds the message data
    bool * complete; //!< Pointer to a value to say the operation is complete
}; //!< Message format for the cor internal functions

struct message_helper_internal_raw_message_s
{
    long type;
    union
    {
        char msg[sizeof(struct message_helper_internal_message_data_s)];
        struct message_helper_internal_message_data_s decoded;
    };
}; //!<Structure used with the internal raw message data

struct message_helper_message_data_s
{
	char msg[MAX_MSG_SIZE];
	uint32_t msg_size;
}; //!< Structure that details helper message data

struct message_helper_raw_message_s
{
	long type;
	union{
		char msg[sizeof(struct message_helper_message_data_s)];
		struct message_helper_message_data_s decoded;
	};
}; //!< Structure used to send message data

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct message_helper_module_data_s mh_module_data = {
		.module_helper = NULL,
		.QId = -1
}; //!< Static variable holding the modules data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**-------------------------------------------------------------------------------**/
/**------------------------- Generic Helper Functions ----------------------------**/
/**-------------------------------------------------------------------------------**/

/**
 * Initialize the ipc message queue
 */
static inline int init_msg_helper_msg_queue(void)
{
    return create_new_queue();
}

/**
 * Function that destroys a queue by id
 * @param id
 * @returns true on success, false otherwise
 */
static bool destroy_msg_queue(int id)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	bool result = true;
    if(msgctl(id, IPC_RMID, NULL) == -1)
    {
        result = false;
    }
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    return result;
}

/**
 * Function that destroys an instance of the helper
 * @param helper
 */
static void destroy_instance(Message_Helper_Instance_t *helper)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != helper);
	int clean_count = 0;
	for(int i=0; i<ARRAY_MAX_COUNT(mh_module_data.reg); i++)
	{
		if(helper == &mh_module_data.reg[i].instance && true == mh_module_data.reg[i].in_use)
		{
			mh_module_data.reg[i].in_use = false;
			//destroy the worker thread
			thread_helper_thread_destroy(helper->Worker_Thread);
			//destroy the queue
			if(false == destroy_msg_queue(helper->QueueId))
			{
				ST_LOG_ERROR("Failed to destroy queue for helper %s\r\n", helper->name);
				SS_ASSERT(true == false);
			}
			clean_count++;
		}
	}
	SS_ASSERT(1 == clean_count);
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**-------------------------------------------------------------------------------**/
/**---------------------- Functions Used By Each Helper --------------------------**/
/**-------------------------------------------------------------------------------**/

/**
 * Function that sends a message
 * @param msg
 */
static void internal_message_send(Message_Helper_Instance_t *helper, struct message_helper_raw_message_s * msg)
{
	int result;
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != helper && NULL != msg);
	msg->type = 1;
	result = msgsnd(helper->QueueId, msg, sizeof(msg->decoded), 0);
	SS_ASSERT(0 == result);
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * Worker function for individual message helpers
 * @param data
 */
static void * msg_helper_message_worker(void * data)
{
	int result;
	struct message_helper_raw_message_s in_message;
	Message_Helper_Instance_t * typed;
	typed = data;
	SS_ASSERT(NULL != typed);
	SS_ASSERT(NULL != typed->cb);
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	while(1)
	{
		result = msgrcv(typed->QueueId, &in_message, sizeof(struct message_helper_raw_message_s), 1, 0);
		if(result != -1)
		{
			typed->cb(in_message.decoded.msg, in_message.decoded.msg_size);
		}
		else
		{
			ST_LOG_ERROR("%s error msgrcv failed with error %i\r\n", __FUNCTION__, errno);
			SS_ASSERT(true == false);
		}
	}
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	return NULL;
}

/**-------------------------------------------------------------------------------**/
/**-------------------------- Thread Safety Functions ----------------------------**/
/**-------------------------------------------------------------------------------**/


/**
 * Function that sends an internal message and waits for its response
 * @param msg
 */
static void internal_message_send_and_wait(struct message_helper_internal_raw_message_s * msg)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	int result;
	bool comp = false;
	msg->decoded.complete = &comp;
	msg->type = 1;
	result = msgsnd(mh_module_data.QId, msg, sizeof(msg->decoded), 0);
	SS_ASSERT(0 == result);
	while(false == comp){}
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * Function that handles creating a new message helper
 * @param msg
 */
static void handle_msg_create(struct message_helper_internal_message_data_s * msg)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	Message_Helper_Instance_t * helper;
	helper = *msg->msg_data.create.result;
	SS_ASSERT(NULL ==  helper);
	for(int i=0; i<ARRAY_MAX_COUNT(mh_module_data.reg) && NULL == helper; i++)
	{
		if(false == mh_module_data.reg[i].in_use)
		{
			mh_module_data.reg[i].in_use = true;
			helper = &mh_module_data.reg[i].instance;
			PRINT_MSG("\t%s helper set to %p\r\n", __FUNCTION__, helper);
			helper->QueueId = init_msg_helper_msg_queue();
			helper->cb = msg->msg_data.create.worker;
			helper->name = msg->msg_data.create.name;
			helper->Worker_Thread = thread_helper_thread_create(msg_helper_message_worker, helper);
			*msg->msg_data.create.result = helper;
		}
	}
	SS_ASSERT(NULL !=  helper);
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * Function that handles removing a message helper
 * @param msg
 */
static void handle_msg_destroy(struct message_helper_internal_message_data_s * msg)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	destroy_instance(msg->msg_data.destroy.instance);
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * The modules thread safety worker function
 * @param data
 */
static void * module_worker(void * data)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	int result;
	struct message_helper_internal_raw_message_s in_message;
	while(1)
	{
		result = msgrcv(mh_module_data.QId, &in_message, sizeof(struct message_helper_internal_raw_message_s), 1, 0);
        if(result != -1)
        {
            SS_ASSERT(1 == in_message.type);
            switch(in_message.decoded.action)
            {
            case MH_CREATE:
            	handle_msg_create(&in_message.decoded);
            	break;
            case MH_DESTROY:
            	handle_msg_destroy(&in_message.decoded);
            	break;
			default:
				SS_ASSERT(true == false);
				break;
            }
            in_message.decoded.complete[0] = true;
        }
        else
        {
            ST_LOG_ERROR("%s error msgrcv failed with error %i\r\n", __FUNCTION__, errno);
            SS_ASSERT(true == false);
        }
	}
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	return NULL;
}

/**
 * Function that initializes the module if it is required
 */
static void init_if_required(void)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	static bool init_in_progress = false;
	while(true == init_in_progress) {}
	init_in_progress = true;
	if(NULL == mh_module_data.module_helper)
	{
		SS_ASSERT(-1 == mh_module_data.QId);
		for(int i=0; i<ARRAY_MAX_COUNT(mh_module_data.reg); i++)
		{
			mh_module_data.reg[i].in_use = false;
		}
		//Create the message queue
		mh_module_data.QId = init_msg_helper_msg_queue();
		//Create the worker thread
		mh_module_data.module_helper = thread_helper_thread_create(module_worker, NULL);
		SS_ASSERT(NULL != mh_module_data.module_helper);
	}
	init_in_progress = false;
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**-------------------------------------------------------------------------------**/
/**------------------------------- API Functions ---------------------------------**/
/**-------------------------------------------------------------------------------**/

/**
 * @brief Create a new message helper
 * @param worker function pointer to the worker task to call when a new message is received
 * @return NULL on error.  Otherwise pointer to the new message helper
 */
Message_Helper_Instance_t *New_Message_Helper(Message_Helper_On_Message worker, const char * name)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	Message_Helper_Instance_t * result;
	Message_Helper_Instance_t ** r_worker;
	struct message_helper_internal_raw_message_s msg;
	init_if_required();

	result = NULL;
	r_worker = &result;
	PRINT_MSG("\t%s Result Address: %p\r\n", __FUNCTION__, r_worker);


	msg.decoded.action = MH_CREATE;
	msg.decoded.msg_data.create.name = name;
	msg.decoded.msg_data.create.worker = worker;
	msg.decoded.msg_data.create.result = r_worker;
	internal_message_send_and_wait(&msg);

	SS_ASSERT(NULL != result);
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	return result;
}

/**
 * @brief Function that destroys a message helper
 * @param helper pointer to the message helper to destroy
 */
void Remove_Message_Helper(Message_Helper_Instance_t *helper)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	struct message_helper_internal_raw_message_s msg;
	init_if_required();
	msg.decoded.action = MH_DESTROY;
	msg.decoded.msg_data.destroy.instance = helper;
	internal_message_send_and_wait(&msg);
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
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	struct message_helper_raw_message_s out;
	init_if_required();
	SS_ASSERT(NULL != helper && NULL != msg && 0 < message_size && message_size <= MAX_MSG_SIZE);
	memcpy(out.decoded.msg, msg, message_size);
	out.decoded.msg_size = message_size;
	internal_message_send(helper, &out);
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Get the number of pending messages on the message helper
 * @param helper
 * @return The number of pending messages
 */
unsigned int Message_Helper_Pending_Messages(Message_Helper_Instance_t *helper)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	struct msqid_ds data;
	init_if_required();
	SS_ASSERT(NULL != helper);
	SS_ASSERT(0 == msgctl(helper->QueueId, IPC_STAT, &data));
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
	return (unsigned int)data.msg_qnum;
}

/**
 * @brief Function that resets the message helper module
 */
void Message_Helper_Reset(void)
{
	int result;
	int error_val;
	struct message_helper_internal_raw_message_s in_message;

	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	for(int i=0; i<ARRAY_MAX_COUNT(mh_module_data.reg); i++)
	{
		if(true == mh_module_data.reg[i].in_use)
		{
			mh_module_data.reg[i].in_use = false;
			//destroy the queue
			if(false == destroy_msg_queue(mh_module_data.reg[i].instance.QueueId))
			{
				ST_LOG_ERROR("Failed to destroy queue for helper %s\r\n", mh_module_data.reg[i].instance.name);
				SS_ASSERT(true == false);
			}
		}
	}

	//Clear out any pending messages
	if(-1 != mh_module_data.QId)
	{
	do{
		result = msgrcv(mh_module_data.QId, &in_message, sizeof(struct message_helper_internal_raw_message_s), 1, IPC_NOWAIT);
		if(0 > result)
		{
			error_val = errno;
		}
	}while(ENOMSG != error_val);
	}

	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}


/**
 * @brief Function that cleans up all of the message helper information on exit
 */
void Message_Helper_Cleanup(void)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	Message_Helper_Reset();
	mh_module_data.module_helper = NULL;
	destroy_msg_queue(mh_module_data.QId);
	mh_module_data.QId = -1;
	PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

