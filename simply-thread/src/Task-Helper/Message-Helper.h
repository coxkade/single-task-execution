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
#include <Thread-Helper.h>

typedef void (*Message_Helper_On_Message)(void *message, uint32_t message_size);  //!< Typedef for message helper callback

typedef struct Message_Helper_Instance_t
{
    int QueueId;
    Message_Helper_On_Message cb;
    helper_thread_t *Worker_Thread;
    const char *name;
} Message_Helper_Instance_t; //!< structure that holds the data of a message helper instance

/**
 * @brief Create a new message helper
 * @param worker function pointer to the worker task to call when a new message is received
 * @return NULL on error.  Otherwise pointer to the new message helper
 */
Message_Helper_Instance_t *New_Message_Helper(Message_Helper_On_Message worker, const char *name);

/**
 * @brief Function that destroys a message helper
 * @param helper pointer to the message helper to destroy
 */
void Remove_Message_Helper(Message_Helper_Instance_t *helper);

/**
 * @brief Function that sends a message
 * @param helper
 * @param msg
 * @param message_size
 */
void Message_Helper_Send(Message_Helper_Instance_t *helper, void *msg, uint32_t message_size);

/**
 * @brief Get the number of pending messages on the message helper
 * @param helper
 * @return The number of pending messages
 */
unsigned int Message_Helper_Pending_Messages(Message_Helper_Instance_t *helper);

/**
 * @brief Function that resets the message helper module
 */
void Message_Helper_Reset(void);

/**
 * @brief Function that cleans up all of the message helper information on exit
 */
void Message_Helper_Cleanup(void);

#endif /* SIMPLY_THREAD_SRC_TASK_HELPER_MESSAGE_HELPER_H_ */
