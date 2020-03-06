/*
 * Message-Helper.h
 *
 *  Created on: Mar 6, 2020
 *      Author: kade
 */

#ifndef SIMPLY_THREAD_SRC_TASK_HELPER_MESSAGE_HELPER_H_
#define SIMPLY_THREAD_SRC_TASK_HELPER_MESSAGE_HELPER_H_

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

typedef void (*Message_Helper_On_Message)(void * message, uint32_t message_size); //!< Typedef for message helper callback

typedef struct Message_Helper_Instance_t
{
	int QueueId;
	Message_Helper_On_Message cb;
	bool Kill_Worker;
	pthread_t Worker_Thread;
}Message_Helper_Instance_t;

/**
 * @brief Create a new message helper
 * @param worker function pointer to the worker task to call when a new message is received
 * @return NULL on error.  Otherwise pointer to the new message helper
 */
Message_Helper_Instance_t * New_Message_Helper(Message_Helper_On_Message worker);

/**
 * @brief Function that destroys a message helper
 * @param helper pointer to the message helper to destroy
 */
void Remove_Message_Helper(Message_Helper_Instance_t * helper);

/**
 * @brief Function that sends a message
 * @param helper
 * @param msg
 * @param message_size
 */
void Message_Helper_Send(Message_Helper_Instance_t * helper, void * msg, uint32_t message_size);

#endif /* SIMPLY_THREAD_SRC_TASK_HELPER_MESSAGE_HELPER_H_ */
