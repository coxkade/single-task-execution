/**
 * @file que-creator.c
 * @author Kade Cox
 * @date Created: Mar 20, 2020
 * @details
 *
 */


#include <que-creator.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static pthread_mutex_t queue_creator_mutex = PTHREAD_MUTEX_INITIALIZER; //!< Module data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


/**
 * Function that creates a new queue
 * @return id of the created queue
 */
int create_new_queue(void)
{
    int result;
    int error_val;
    assert(0 == pthread_mutex_lock(&queue_creator_mutex));
    result = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    error_val = errno;
    pthread_mutex_unlock(&queue_creator_mutex);
    if(0 > result)
    {
        printf("Failed to create queue %i %i \r\n", result, error_val);
    }
    if(0 > result && 0 != error_val)
    {
        assert(0 < result);
    }
    return result;
}
