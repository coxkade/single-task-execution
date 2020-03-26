/**
 * @file priv-simply-thread.h
 * @author Kade Cox
 * @date Created: Dec 16, 2019
 * @details
 * Private APIS for simply thread
 */

#include <simply-thread-log.h>
#include <TCB.h>
#include <stdlib.h>

#ifndef PRIV_SIMPLY_THREAD_H_
#define PRIV_SIMPLY_THREAD_H_

#ifndef ST_NS_PER_MS
#define ST_NS_PER_MS 1000000
#endif //ST_NS_PER_MS


/**
 * @brief Function that sleeps for the specified number of nanoseconds
 * @param ns number of nanoseconds to sleep
 */
void simply_thread_sleep_ns(unsigned long ns);

#endif /* PRIV_SIMPLY_THREAD_H_ */
