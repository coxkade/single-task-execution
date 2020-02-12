/**
 * @file unlink.c
 * @author Kade Cox
 * @date Created: Feb 12, 2020
 * @details
 * Tool for unlinking all of our created semaphores
 */

#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))
#define MAX_SEM_NUMBER 9000 //The max semaphore count allowed
#define MAX_SEM_NAME_SIZE 100 //The max semaphore name size

#define PRINT_MSG(...) printf(__VA_ARGS__)

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

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
 * @brief check if a semaphore exists if so kill it
 * @param name
 */
static void unlink_sem_by_name(const char *name)
{
    if(0 != sem_unlink(name))
    {
        switch(errno)
        {
            case EACCES:
                PRINT_MSG("EACCES: %s\r\n", name);
                break;
            case ENAMETOOLONG:
                PRINT_MSG("ENAMETOOLONG: %s\r\n", name);
                assert(false);
                break;
            case EINVAL:
                //No semaphore by that name
                break;
            case ENOENT:
                //No semaphore by that name
                break;
            default:
                PRINT_MSG("Unknown Error Number %i\r\n", errno);
                assert(false);
                break;
        }
    }
    else
    {
        PRINT_MSG("Semaphore %s unlinked\r\n", name);
    }
}

/**
 * @brief the main function
 * @return 0
 */
int main(void)
{
    const char *base_string = "simply_thread_semaphore_";
    char name[MAX_SEM_NAME_SIZE];

    for(int i = 0; i < MAX_SEM_NUMBER; i++)
    {
        snprintf(name, MAX_SEM_NAME_SIZE, "%s%i", base_string, i);
        unlink_sem_by_name(name);
    }

}
