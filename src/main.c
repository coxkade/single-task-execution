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

/**
 * @brief internals sleep function
 * @param ms time in ms to sleep
 */
static void m_sleep_ms(long ms);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

pthread_t one = 0;
bool one_block = false;

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

static void user_1_catch(int signo)
{
    assert(SIGUSR1 == signo);
    one_block = true;
    while(true == one_block)
    {
        m_sleep_ms(1);
    }
}

static void user_2_catch(int signo)
{
    assert(SIGUSR2 == signo);
    one_block = false;
}

/**
 * @brief internals sleep function
 * @param ms time in ms to sleep
 */
static void m_sleep_ms(long ms)
{
    static const long ms_in_sec = 1000;
    static const long ns_in_ms = 1E6;
    struct timespec time_data =
    {
        .tv_sec = 0,
        .tv_nsec = 0
    };
    if(ms >= ms_in_sec)
    {
        time_data.tv_sec = ms / ms_in_sec;
        ms = ms - (time_data.tv_sec * ms_in_sec);
    }
    if(0 < ms)
    {
        time_data.tv_nsec = ms * ns_in_ms;
    }
    while(0 != nanosleep(&time_data, &time_data))
    {
    }
}

/**
 * @brief the first worker thread
 * @param ptr
 */
static void *thread_one_worker(void *ptr)
{
    printf("%s Started\r\n", __FUNCTION__);
    while(1)
    {
        m_sleep_ms(100);
        printf("\t%s Running\r\n", __FUNCTION__);
    }
    return NULL;
}


/**
 * The Main Function
 * @return
 */
int main(void)
{
    int result;
    printf("Starting the main function\r\n");
    signal(SIGUSR1, user_1_catch);
    signal(SIGUSR2, user_2_catch);
    printf("Launching the threads\r\n");
    pthread_assert(pthread_create(&one, NULL, thread_one_worker, NULL));
    m_sleep_ms(1000);
    printf("Telling the threads to stop\r\n");
    pthread_assert(pthread_kill(one, SIGUSR1));
    m_sleep_ms(5000);
    printf("Telling the threads to resume");
    pthread_assert(pthread_kill(one, SIGUSR2));
    m_sleep_ms(1000);
    printf("main exiting\r\n");
    return 0;
}
