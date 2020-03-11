/**
 * @file simply_thread_system_clock.c
 * @author Kade Cox
 * @date Created: Feb 28, 2020
 * @details
 * Module for the system clock for the simply thread library
 */

#include <simply_thread_system_clock.h>
#include <Thread-Helper.h>
#include <TCB.h>
#include <priv-simply-thread.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))
//Macro that sets the registry size
#define REGISTRY_MAX_COUNT (100)

#ifdef DEBUG_SIMPLY_THREAD
#define PRINT_MSG(...) simply_thread_log(COLOR_EARTH_GREEN, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#endif //DEBUG_SIMPLY_THREAD

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/


typedef void (*on_reg_cb)(sys_clock_on_tick_handle_t handle, uint64_t new_tick, void *args);  //!< Typedef for the on tick registry

struct sysclock_on_tick_registry_element_s
{
    on_reg_cb cb;
    void *args;
    bool enabled;
}; //!< Structure that holds the registry elements.

struct system_clock_data_s
{
    const uint64_t max_ticks; //!< The Maximum number of ticks possible before roll over
    uint64_t current_ticks; //!< The Current tick count
    struct sysclock_on_tick_registry_element_s tick_reg[REGISTRY_MAX_COUNT]; //!< The tick registery
    helper_thread_t * tick_thread; //!< The tick worker thread
    bool clock_running; //!< Tells if the system clock is currently running
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct system_clock_data_s clock_module_data =
{
    .max_ticks = SIMPLY_THREAD_MAX_SYSTEM_CLOCK_TICKS,
    .current_ticks = 0,
    .tick_thread = NULL
}; //!< Variable that holds the local modules data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

/**
 * The Tick worker function
 * @param data
 */
static void * on_tick_worker(void * data)
{
	sys_clock_on_tick_handle_t worker_handle;
	struct sysclock_on_tick_registry_element_s worker;

	while(1)
	{
		simply_thread_sleep_ns(ST_NS_PER_MS);
		clock_module_data.current_ticks++;
		for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg); i++)
		{
			worker_handle = &clock_module_data.tick_reg[i];
			memcpy(&worker, &clock_module_data.tick_reg[i], sizeof(worker));
			if(NULL != worker.cb && true == worker.enabled)
			{
				//Call the callback here
				worker.cb(worker_handle, clock_module_data.current_ticks, worker.args);
			}
		}
	}
	return NULL;
}

/**
 * @brief Function that initializes the thread worker
 * @param data
 */
static void init_thread_worker(void * data)
{
	assert(NULL == data);
	if(NULL == clock_module_data.tick_thread)
	{
		clock_module_data.tick_thread = thread_helper_thread_create(on_tick_worker, NULL);
		assert(NULL != clock_module_data.tick_thread);
		for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg); i++)
		{
			clock_module_data.tick_reg[i].cb = NULL;
		}
		clock_module_data.clock_running = true;
		clock_module_data.current_ticks = 0;
	}
}

/**
 * @brief Function that initializes the module if required
 */
static void init_if_required(void)
{
	if(NULL == clock_module_data.tick_thread)
	{
		run_in_tcb_context(init_thread_worker, NULL);
	}
}

/**
 * Function that pauses the clock from the tcb context
 * @param data
 */
static void pause_clock_tcb(void * data)
{
	bool * typed;
	typed = data;
	assert(NULL != typed);
	if(false == clock_module_data.clock_running)
	{
		typed[0] = false;
	}
	else
	{
		assert(true == clock_module_data.clock_running);
		assert(NULL != clock_module_data.tick_thread);
		thread_helper_pause_thread(clock_module_data.tick_thread);
		clock_module_data.clock_running = false;
		typed[0] = true;
	}
}

/**
 * @brief Function that pauses the clock
 */
static void pause_clock(void)
{
	bool paused = false;
	while(false == paused)
	{
		run_in_tcb_context(pause_clock_tcb, &paused);
	}
}

/**
 * Function that resumes the clock from the tcb context
 * @param data
 */
static void resume_clock_tcb(void * data)
{
	bool * typed;
	typed = data;
	assert(NULL != typed);
	if(true == clock_module_data.clock_running)
	{
		typed[0] = false;
	}
	else
	{
		assert(false == clock_module_data.clock_running);
		assert(NULL != clock_module_data.tick_thread);
		thread_helper_run_thread(clock_module_data.tick_thread);
		clock_module_data.clock_running = true;
		typed[0] = true;
	}
}

/**
 * @brief Function that resumes the clock
 */
static void resume_clock(void)
{
	bool resumed = false;
	while(false == resumed)
	{
		run_in_tcb_context(resume_clock_tcb, &resumed);
	}
}

/**
 * @brief Function that resets the simply thread clock logic
 */
void simply_thead_system_clock_reset(void)
{
	if(NULL != clock_module_data.tick_thread)
	{
		thread_helper_thread_destroy(clock_module_data.tick_thread);
		clock_module_data.tick_thread = NULL;
	}
}

/**
 * @brief Function that registers a function to be called on a tick
 * @param on_tick pointer to the function to call on tick
 * @param args argument to pass to the on tick handler when it is called
 * @return NULL on error.  Otherwise the registered id
 */
sys_clock_on_tick_handle_t simply_thead_system_clock_register_on_tick(void (*on_tick)(sys_clock_on_tick_handle_t handle, uint64_t tickval,
        void *args), void *args)
{
	sys_clock_on_tick_handle_t rv;
	rv = NULL;
	init_if_required();
	pause_clock();
	assert(false == clock_module_data.clock_running);
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg) && NULL == rv; i++)
	{
		if(NULL == clock_module_data.tick_reg[i].cb)
		{
			clock_module_data.tick_reg[i].cb = on_tick;
			clock_module_data.tick_reg[i].args = args;
			clock_module_data.tick_reg[i].enabled = true;
			rv = &clock_module_data.tick_reg[i];
		}
	}
	resume_clock();
	return rv;
}

/**
 * @brief Function the deregisters a function on tick
 * @param handle the handle to deregister
 */
void simply_thead_system_clock_deregister_on_tick(sys_clock_on_tick_handle_t handle)
{
	struct sysclock_on_tick_registry_element_s * typed;
	typed = handle;
	init_if_required();
	assert(NULL != typed);
	pause_clock();
	typed->cb = NULL;
	typed->args = NULL;
	typed->enabled = false;
	resume_clock();
}

/**
 * Function used to disable the on tick from the on tick handler
 * @param handle the handle of the handler
 */
void simply_thead_system_clock_disable_on_tick_from_handler(sys_clock_on_tick_handle_t handle)
{
	struct sysclock_on_tick_registry_element_s * typed;
	typed = handle;
	init_if_required();
	assert(NULL != typed);
	pause_clock();
	typed->enabled = false;
	resume_clock();
}

/**
 * Function that calculates the tick value
 * @param ticks the ticks to add to the current ticks
 * @return the the ticks after the added ticks
 */
uint64_t simply_thread_system_clock_tick_in(uint64_t ticks)
{
	uint64_t rv;
	init_if_required();
	pause_clock();
	rv =  clock_module_data.current_ticks + ticks;
	resume_clock();
	return rv;
}
