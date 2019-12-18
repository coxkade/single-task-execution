/**
 * @file simply-thread-linked-list.h
 * @author Kade Cox
 * @date Created: Dec 16, 2019
 * @details
 * Module for a simply thread linked list
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef SIMPLY_THREAD_LINKED_LIST_H_
#define SIMPLY_THREAD_LINKED_LIST_H_

//Define for an invalid handle
#define SIMPLY_THREAD_LL_INVALID (NULL)

typedef void *simply_thread_linked_list_t;  //!< Typedef for linked list handle

/**
 * @brief creates a new linked list with elements of a specific size
 * @param element_size
 * @return the new linked list handle
 */
simply_thread_linked_list_t simply_thread_new_ll(unsigned long element_size);

/**
 * @brief remove a node with a specified index
 * @param node_num
 * @return true on success
 */
bool simply_thread_ll_remove(simply_thread_linked_list_t handle, unsigned int node_num);

/**
 * @brief append a new value at the end of the list
 * @param handle
 * @param data
 * @return true on success
 */
bool simply_thread_ll_append(simply_thread_linked_list_t handle, void *data);

/**
 * @brief fetch the data at a node
 * @param handle
 * @param node_num
 */
void *simply_thread_ll_get(simply_thread_linked_list_t handle, unsigned int node_num);

/**
 * @brief get the final element in the list
 * @param handle
 */
void *simply_thread_ll_get_final(simply_thread_linked_list_t handle);

/**
 * @brief set the data of a node
 * @param handle
 * @param node_num
 * @param data
 * @return
 */
bool simply_thread_ll_set(simply_thread_linked_list_t handle, unsigned int node_num, void *data);

/**
 * @brief Function that gets the number of nodes in the linked list
 * @param handle
 * @return
 */
unsigned int simply_thread_ll_count(simply_thread_linked_list_t handle);

/**
 * @brief destroy a linked list
 * @param handle
 */
void simply_thread_ll_destroy(simply_thread_linked_list_t handle);

/**
 * @brief Small test for the linked lists.
 */
void simply_thread_ll_test(void);

#endif /* SIMPLY_THREAD_LINKED_LIST_H_ */
