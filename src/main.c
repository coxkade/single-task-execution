/**
 * @file main.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 * Experimental program
 */

#include <simply-thread.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>


/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define pthread_assert(V) do{\
    int p_assert_rv = V;\
    if(0 != p_assert_rv) { printf("\tError rv: %d\r\n", p_assert_rv); };\
    assert(0 == p_assert_rv);\
    }while(0)



/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/


/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static simply_thread_task_t task_one = NULL;  //The First tasks task handle
static simply_thread_task_t task_two = NULL; //The Second tasks task handle

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/



/**
 * @brief the task function for the second task
 * @param data UNUSED
 * @param data_size UNUSED
 */
static void thread_two_worker(void *data, uint16_t data_size)
{
    simply_thread_sleep_ms(100);
    while(1)
    {
        // printf("%s running\r\n", __FUNCTION__);
        assert(true == simply_thread_task_suspend(NULL));
    }
}

/**
 * @brief The function for the first task
 * @param data UNUSED
 * @param data_size UNUSED
 */
static void thread_one_worker(void *data, uint16_t data_size)
{
    simply_thread_sleep_ms(100);
    while(1)
    {
        // printf("%s running\r\n", __FUNCTION__);
        assert(true == simply_thread_task_resume(task_two));
        simply_thread_sleep_ms(10);
    }
}

/**
 * The Main Function
 * @return
 */
int main(void)
{
    simply_thread_reset();
    task_one = simply_thread_new_thread("TASK1", thread_one_worker, 1, NULL, 0);
    task_two = simply_thread_new_thread("TASK2", thread_two_worker, 3, NULL, 0);

    sleep(60 * 3);
    simply_thread_cleanup();
    return 0;
}
