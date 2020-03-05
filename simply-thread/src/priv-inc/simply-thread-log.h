/**
 * @file simply-thread-log.h
 * @author Kade Cox
 * @date Created: Dec 13, 2019
 * @details
 * Module for printing log messages on the simply thread library
 */

#ifndef SIMPLY_THREAD_LOG_H_
#define SIMPLY_THREAD_LOG_H_

//Colors to use with the logger
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_RED "\x1b[31m"
#define COLOR_BLUE "\x1b[34m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_BLACK "\x1b[30m"
#define COLOR_CYAN "\x1b[36m"
#define COLOR_WHITE "\x1b[37m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_LIGHT_GREEN "\x001b[38;5;154m"
#define COLOR_PURPLE "\x001b[38;5;93m"
#define COLOR_EARTH_GREEN "\x001b[38;5;64m"
#define COLOR_ORANGE "\x001b[38;5;202m"
#define COLOR_SKY_BLUE "\x001b[38;5;87m"
#define COLOR_RESET   "\x1b[0m"

//Macro for printing error messages
#define ST_LOG_ERROR(...) simply_thread_log(COLOR_RED, __VA_ARGS__);

/**
 * @brief Function that  prints a message in color
 * @param fmt Standard printf format
 */
void simply_thread_log(const char *color, const char *fmt, ...);

/**
 * @brief Block any pending writes
 */
void simply_thread_log_lock(void);

/**
 * @brief Unblock any pending writes
 */
void simply_thread_log_unlock(void);


#endif /* SIMPLY_THREAD_LOG_H_ */
