/**
 * @file simply-thread-linked-list.c
 * @author Kade Cox
 * @date Created: Dec 16, 2019
 * @details
 * Linked lists used by the library
 */

#include <simply-thread-linked-list.h>
#include <simply-thread-log.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

//macro that handles some casting for me
#define HANDLE_FIX(x) ((struct m_ll_data_s *)x)

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct m_module_node_s; //Forward declare our node

struct m_module_node_s
{
    void *data;  //!< The data
    unsigned int data_size; //!< The size of the data
    struct m_module_node_s *nxt; //!< Pointer to the next Node
    struct m_module_node_s *prv; //!< Pointer to the previous Node
}; //!< Structure of a single linked list node

struct m_ll_data_s
{
    struct m_module_node_s  *root;  //!< Pointer to the root node
    unsigned int data_size; //!< The size of each nodes data
}; //!< The main linked list structure

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief function that creates a new node stuffed with the appropriate data
 * @param data
 * @param data_size
 * @return
 */
static struct m_module_node_s *m_new_node(void *data, unsigned int data_size)
{
    struct m_module_node_s *rv;
    rv = malloc(sizeof(struct m_module_node_s));
    assert(NULL != rv);
    rv->data = malloc(data_size);
    assert(NULL != rv->data);
    rv->data_size = data_size;
    memcpy(rv->data, data, data_size);
    rv->nxt = NULL;
    rv->prv = NULL;
    return rv;
}


/**
 * @brief creates a new linked list with elements of a specific size
 * @param element_size
 * @return the new linked list handle
 */
simply_thread_linked_list_t simply_thread_new_ll(unsigned long element_size)
{
    struct m_ll_data_s *new_list;

    new_list = malloc(sizeof(struct m_ll_data_s));
    assert(NULL != new_list);
    new_list->root = NULL;
    new_list->data_size = element_size;
    return (simply_thread_linked_list_t)new_list;
}

/**
 * @brief remove a node with a specified index
 * @param node_num
 * @return true on success
 */
bool simply_thread_ll_remove(simply_thread_linked_list_t handle, unsigned int node_num)
{
    struct m_module_node_s *current;
    unsigned int current_count = 0;

    current = HANDLE_FIX(handle)->root;
    while(NULL != current)
    {
        if(current_count == node_num)
        {
            if(NULL == current->prv)
            {
                HANDLE_FIX(handle)->root = current->nxt;
                if(NULL != current->nxt)
                {
                    current->nxt->prv = NULL;
                }
            }
            else
            {
                current->prv->nxt = current->nxt;
                current->nxt->prv = current->prv;
            }
            free(current->data);
            free(current);
            return true;
        }
        current_count++;
    }

    return false;
}

/**
 * @brief append a new value at the end of the list
 * @param handle
 * @param data
 * @return true on success
 */
bool simply_thread_ll_append(simply_thread_linked_list_t handle, void *data)
{
    struct m_module_node_s *current;
    struct m_module_node_s *new_node;
    current = HANDLE_FIX(handle)->root;

    new_node = m_new_node(data, HANDLE_FIX(handle)->data_size);

    if(NULL == current)
    {
        //This is the first element
        HANDLE_FIX(handle)->root = new_node;
        return true;
    }

    while(NULL != current->nxt)
    {
        current = current->nxt;
    }

    current->nxt = new_node;
    new_node->prv = current;
    return true;
}

/**
 * @brief fetch the data at a node
 * @param handle
 * @param node_num
 */
void *simply_thread_ll_get(simply_thread_linked_list_t handle, unsigned int node_num)
{
    unsigned int current_count = 0;
    struct m_module_node_s *current;
    current = HANDLE_FIX(handle)->root;
    while(NULL != current)
    {
        if(current_count == node_num)
        {
            return current->data;
        }
        current_count++;
        current = current->nxt;
    }

    return NULL;
}

/**
 * @brief get the final element in the list
 * @param handle
 */
void *simply_thread_ll_get_final(simply_thread_linked_list_t handle)
{
    struct m_module_node_s *current;
    current = HANDLE_FIX(handle)->root;
    if(NULL != current)
    {
        while(NULL != current->nxt)
        {
            current = current->nxt;
        }
        return current->data;
    }
    return NULL;
}

/**
 * @brief set the data of a node
 * @param handle
 * @param node_num
 * @param data
 * @return
 */
bool simply_thread_ll_set(simply_thread_linked_list_t handle, unsigned int node_num, void *data)
{
    unsigned int current_count = 0;
    struct m_module_node_s *current;
    current = HANDLE_FIX(handle)->root;
    while(NULL != current)
    {
        if(current_count == node_num)
        {
            memcpy(current->data, data, current->data_size);
            return true;
        }
        current_count++;
        current = current->nxt;
    }

    return false;
}

/**
 * @brief Function that gets the number of nodes in the linked list
 * @param handle
 * @return
 */
unsigned int simply_thread_ll_count(simply_thread_linked_list_t handle)
{
    unsigned int rv = 0;
    struct m_module_node_s *current;
    current = HANDLE_FIX(handle)->root;

    while(NULL != current)
    {
        rv++;
        current = current->nxt;
    }

    return rv;
}

/**
 * @brief destroy a linked list
 * @param handle
 */
void simply_thread_ll_destroy(simply_thread_linked_list_t handle)
{
    struct m_module_node_s *current;
    struct m_module_node_s *worker;
    if(NULL != handle)
    {
        current = HANDLE_FIX(handle)->root;
        while(NULL != current)
        {
            worker = current->nxt;
            free(current->data);
            free(current);
            current = worker;
        }
        free(handle);
    }
}

/**
 * @brief Small test for the linked lists.
 */
void simply_thread_ll_test(void)
{
	simply_thread_linked_list_t handle;
	int i = 1;
	int * p;
	handle = simply_thread_new_ll(sizeof(int));
	assert(true == simply_thread_ll_append(handle, &i));
	i = 2;
	assert(true == simply_thread_ll_append(handle, &i));
	i = 3;
	assert(true == simply_thread_ll_append(handle, &i));
	assert(3 == simply_thread_ll_count(handle));

	p = simply_thread_ll_get(handle, 0);
	assert(1 == *p);
	p = simply_thread_ll_get(handle, 1);
	assert(2 == *p);
	p = simply_thread_ll_get(handle, 2);
	assert(3 == *p);

	simply_thread_ll_remove(handle, 0);
	assert(2 == simply_thread_ll_count(handle));
	p = simply_thread_ll_get(handle, 0);
	assert(2 == *p);
	p = simply_thread_ll_get(handle, 1);
	assert(3 == *p);

	simply_thread_ll_remove(handle, 0);
	assert(1 == simply_thread_ll_count(handle));
	p = simply_thread_ll_get(handle, 0);
	assert(3 == *p);

	simply_thread_ll_remove(handle, 0);
	assert(0 == simply_thread_ll_count(handle));

	simply_thread_ll_destroy(handle);
//	assert(true == false);
}
