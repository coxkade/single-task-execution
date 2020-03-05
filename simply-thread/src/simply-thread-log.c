/**
 * @file simply-thread-log.c
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 *
 */

#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>

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

static pthread_mutex_t module_mutex = PTHREAD_MUTEX_INITIALIZER;

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief Function that  prints a message in color
 * @param fmt Standard printf format
 */
void simply_thread_log(const char *color, const char *fmt, ...)
{
    char final_buffer[2048];
    char time_buffer[400];
    time_t t;
    struct tm tm;
    va_list args;
    char *print_buffer = NULL;
    unsigned int buffer_size;

    simply_thread_log_lock();

    //Setup the time message string
    t = time(NULL);
    tm = *localtime(&t);
    snprintf(time_buffer, ARRAY_MAX_COUNT(time_buffer), "%d:%02d:%02d task:%p ", tm.tm_hour, tm.tm_min, tm.tm_sec, pthread_self());

    va_start(args, fmt);
    int rc = vsnprintf(final_buffer, ARRAY_MAX_COUNT(final_buffer), fmt, args);
    assert(0 < rc);
    va_end(args);
    if(NULL != color)
    {
		buffer_size = strlen(time_buffer) + strlen(final_buffer) + strlen(color) + strlen(COLOR_RESET) + 10;
		print_buffer = malloc(buffer_size);
		assert(NULL != print_buffer);
		snprintf(print_buffer, buffer_size, "%s%s%s%s", color, time_buffer, final_buffer, COLOR_RESET);
    }
    else
    {
    	buffer_size = strlen(time_buffer) + strlen(final_buffer) + 10;
		print_buffer = malloc(buffer_size);
		assert(NULL != print_buffer);
		snprintf(print_buffer, buffer_size, "%s%s", time_buffer, final_buffer);
    }
    printf("%s", print_buffer);
    free(print_buffer);
    simply_thread_log_unlock();
}

/**
 * @brief Block any pending writes
 */
void simply_thread_log_lock(void)
{
	assert(0 == pthread_mutex_lock(&module_mutex));
}

/**
 * @brief Unblock any pending writes
 */
void simply_thread_log_unlock(void)
{
	pthread_mutex_unlock(&module_mutex);
}

