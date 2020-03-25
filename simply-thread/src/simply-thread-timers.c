/**
 * @file simply-thread-timers.c
 * @author Kade Cox
 * @date Created: Jan 20, 2020
 * @details
 * module that manages timers created by the simply thread library
 */

#include <simply-thread-timers.h>
#include <simply-thread-log.h>
#include <priv-simply-thread.h>
#include <simply_thread_system_clock.h>
#include <Sem-Helper.h>
#include <TCB.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

//Macro that casts the handle data
#define TIMER_DATA(x) ((struct single_timer_data_s *)x)

//#define DEBUG_SIMPLY_THREAD

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_SKY_BLUE, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

//The maximum number of supported timers
#ifndef SIMPLE_THREAD_MAX_TIMERS
#define SIMPLE_THREAD_MAX_TIMERS (250)
#endif //SIMPLE_THREAD_MAX_TIMERS

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct simply_thread_timer_entry_s
{
    sys_clock_on_tick_handle_t tick_handle;
    bool running;
    uint64_t count;
    uint64_t max_count;
    simply_thread_timer_cb cb;
    const char *name;
    simply_thread_timer_type_e mode;
}; //!< Structure that contains the data used by the timer registry.

struct simply_thread_timer_module_data_s
{
    bool initialized;
    struct simply_thread_timer_entry_s timer_registry[SIMPLE_THREAD_MAX_TIMERS];
};//!< Structure that holds the data for this module

struct st_timer_create_data_s
{
	simply_thread_timer_cb cb;
	const char *name;
	unsigned int period_ms;
	simply_thread_timer_type_e mode;
	bool run_now;
	 struct simply_thread_timer_entry_s * result;
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct simply_thread_timer_module_data_s m_timer_data =
{
    .initialized = false
}; //!< Variable that holds this modules local data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * @brief On tick worker function for the module;
 * @param handle
 * @param tickval
 * @param args
 */
static void simply_thread_timers_worker(sys_clock_on_tick_handle_t handle, uint64_t tickval, void *args)
{
    struct simply_thread_timer_entry_s *typed;
    struct simply_thread_timer_entry_s worker;
    typed = args;
    SS_ASSERT(NULL != typed);
    memcpy(&worker, typed, sizeof(worker));
    SS_ASSERT(NULL != worker.cb);
    if(true == worker.running && NULL != worker.tick_handle)
    {
        worker.count++;
        if(worker.count >= worker.max_count)
        {
            PRINT_MSG("\tTriggering Timer: %s\r\n", worker.name);
            worker.count = 0;
            worker.cb(typed);
            PRINT_MSG("\tTimer Triggered\r\n");
            if(SIMPLY_THREAD_TIMER_ONE_SHOT == worker.mode)
            {
                PRINT_MSG("\t%s setting running to false\r\n");
                worker.running = false;
            }
        }
        memcpy(typed, &worker, sizeof(worker));
    }

}


/**
 * @brief Function that initializes this module from the TCB context
 * @param data
 */
static void st_timers_init(void * data)
{
	if(false == m_timer_data.initialized)
	{
		for(unsigned int i=0; i<ARRAY_MAX_COUNT(m_timer_data.timer_registry); i++)
		{
			m_timer_data.timer_registry[i].cb = NULL;
			m_timer_data.timer_registry[i].count = 0;
			m_timer_data.timer_registry[i].max_count = 0;
			m_timer_data.timer_registry[i].name = NULL;
			m_timer_data.timer_registry[i].running = false;
			m_timer_data.timer_registry[i].tick_handle = NULL;
		}
		m_timer_data.initialized = true;
	}
}

/**
 * Initialize this module if required
 */
static void st_timers_init_if_needed(void)
{
	if(false == m_timer_data.initialized)
	{
		run_in_tcb_context(st_timers_init, NULL);
	}
}


/**
 * @brief Destroy all simply thread timers
 */
void simply_thread_timers_cleanup(void)
{
	m_timer_data.initialized = false;
}

/**
 * Create a new timer from the TCB context
 * @param data
 */
static void st_timer_create_tcb(void * data)
{
	struct st_timer_create_data_s * typed;
	typed = data;
	PRINT_MSG("Running %s\r\n", __FUNCTION__);
	SS_ASSERT(NULL != typed);
	typed->result = NULL;


	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(m_timer_data.timer_registry) && NULL == typed->result; i++)
	{
		PRINT_MSG("\tindex %u\r\n", i);
		if(m_timer_data.timer_registry[i].name == NULL)
		{
			typed->result = &m_timer_data.timer_registry[i];
			m_timer_data.timer_registry[i].cb = typed->cb;
			m_timer_data.timer_registry[i].name = typed->name;
			m_timer_data.timer_registry[i].max_count = typed->period_ms;
			m_timer_data.timer_registry[i].count = 0;
			m_timer_data.timer_registry[i].running = false;
			m_timer_data.timer_registry[i].tick_handle = NULL;
			m_timer_data.timer_registry[i].mode = typed->mode;
			PRINT_MSG("\tCreated timer at index %u\r\n", i);
		}
	}
}

/**
 * @brief Function that creates a new timer
 * @param cb callback function to trigger when time elapses
 * @param name the name of the timer
 * @param period_ms the period in milliseconds
 * @param mode the mode of the timer, repeat etc.
 * @param run_now if true start the timer now
 * @return handle of the new timer, NULL on error
 */
simply_thread_timer_t simply_thread_create_timer(simply_thread_timer_cb cb, const char *name, unsigned int period_ms, simply_thread_timer_type_e mode,
        bool run_now)
{
	struct st_timer_create_data_s run_data;
	run_data.cb = cb;
	run_data.name = name;
	run_data.mode = mode;
	run_data.period_ms = period_ms;
	run_data.run_now = run_now;
	run_data.result = NULL;
	st_timers_init_if_needed();
	if(NULL == cb || NULL == name || 0 >= period_ms)
	{
		return NULL;
	}
	SS_ASSERT(SIMPLY_THREAD_TIMER_ONE_SHOT == mode || SIMPLY_THREAD_TIMER_REPEAT == mode);
	run_in_tcb_context(st_timer_create_tcb, &run_data);
	if(NULL != run_data.result)
	{
		run_data.result->tick_handle = simply_thead_system_clock_register_on_tick(simply_thread_timers_worker, run_data.result);
		if(true == run_data.run_now)
		{
			simply_thread_timer_start(run_data.result);
		}
	}
    return run_data.result;
}

/**
 * @brief Function that starts a simply thread timer
 * @param timer the handle of the timer to start
 * @return true on success
 */
bool simply_thread_timer_start(simply_thread_timer_t timer)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    struct simply_thread_timer_entry_s worker;
    if(NULL == timer)
    {
        return false;
    }
    memcpy(&worker, timer, sizeof(worker));
    SS_ASSERT(NULL != worker.cb && NULL != worker.name);
    worker.count = 0;
    worker.running = true;
    memcpy(timer, &worker, sizeof(worker));
    return true;
}

/**
 * @brief Function that stops a simply thread timer
 * @param timer the handle of the timer to stop
 * @return true on success
 */
bool simply_thread_timer_stop(simply_thread_timer_t timer)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    struct simply_thread_timer_entry_s  worker;
    if(NULL == timer)
    {
        return false;
    }
    memcpy(&worker, timer, sizeof(worker));
    worker.running = false;
    memcpy(timer, &worker, sizeof(worker));
    return true;
}

