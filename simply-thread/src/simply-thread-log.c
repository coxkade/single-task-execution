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

#ifndef SIMPLY_THREAD_LOG_BUFFER_SIZE
#define SIMPLY_THREAD_LOG_BUFFER_SIZE 1024
#endif //SIMPLY_THREAD_LOG_BUFFER_SIZE

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
 * @brief Function that  prints a message in color
 * @param fmt Standard printf format
 */
void simply_thread_log(const char *color, const char *fmt, ...)
{
    char final_buffer[SIMPLY_THREAD_LOG_BUFFER_SIZE];
    char time_buffer[100];
    char print_buffer[SIMPLY_THREAD_LOG_BUFFER_SIZE];
    time_t t;
    struct tm tm;
    va_list args;
    unsigned int buffer_size;

    //Setup the time message string
    t = time(NULL);
    tm = *localtime(&t);
    snprintf(time_buffer, ARRAY_MAX_COUNT(time_buffer), "%d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);

    va_start(args, fmt);
    int rc = vsnprintf(final_buffer, ARRAY_MAX_COUNT(final_buffer), fmt, args);
    assert(0 < rc);
    va_end(args);
    buffer_size = strlen(time_buffer) + strlen(final_buffer) + strlen(color) + strlen(COLOR_RESET) + 10;
    assert(NULL != print_buffer);
    snprintf(print_buffer, buffer_size, "%s%s%s%s", color, time_buffer, final_buffer, COLOR_RESET);
    printf("%s", print_buffer);
}
