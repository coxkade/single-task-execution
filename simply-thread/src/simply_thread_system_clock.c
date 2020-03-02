/**
 * @file simply_thread_system_clock.c
 * @author Kade Cox
 * @date Created: Feb 28, 2020
 * @details
 * Module for the system clock for the simply thread library
 */

#include <simply_thread_system_clock.h>
#include <priv-simply-thread.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include "priv-inc/master-mutex.h"

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
}; //!< Structure that holds the registry elements.

struct system_clock_data_s
{
    const uint64_t max_ticks; //!< The Maximum number of ticks possible before roll over
    uint64_t current_ticks; //!< The Current tick count
    struct sysclock_on_tick_registry_element_s tick_reg[REGISTRY_MAX_COUNT]; //!< The tick registery
    bool initialized; //!< Tells if the module has been initialize
    bool pause_clock; //!< Tells the clock to pause
    bool clock_running; //!< Tells us if the clock is currently incrementing
    pthread_t worker_handle; //!< The pthread of the worker thread
    bool kill_worker; //!< Tells the worker thread to exit
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
    .initialized = false
}; //!< Variable that holds the local modules data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/

static void worker_wait_if_needed(void)
{
    if(true == clock_module_data.pause_clock)
    {
        PRINT_MSG("%s waiting\r\n", __FUNCTION__);
        clock_module_data.clock_running = false;
        while(true == clock_module_data.pause_clock) {} //Wait till we do not want the clock paused
        clock_module_data.clock_running = true;
    }
}

/**
 * @brief the sys clock worker thread
 * @param arg
 */
static void *sys_clock_worker(void *arg)
{
    sys_clock_on_tick_handle_t worker_handle;
    struct sysclock_on_tick_registry_element_s worker;
    assert(NULL == arg);
    clock_module_data.clock_running = true;
    while(false == clock_module_data.kill_worker)
    {
        worker_wait_if_needed();
        simply_thread_sleep_ns(ST_NS_PER_MS);
        clock_module_data.current_ticks++;
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg); i++)
        {
            worker_wait_if_needed();
            worker_handle = &clock_module_data.tick_reg[i];
            memcpy(&worker, &clock_module_data.tick_reg[i], sizeof(worker));
            if(NULL != worker.cb)
            {
                //Call the callback here
                worker.cb(worker_handle, clock_module_data.current_ticks, worker.args);
            }
        }
    }
    clock_module_data.clock_running = false;
    return NULL;
}

/**
 * Function that initializes the module if needed
 */
static void init_if_needed(void)
{
    if(false == clock_module_data.initialized)
    {
        MUTEX_GET();
        PRINT_MSG("Initializing the system clock\r\n");
        if(false == clock_module_data.initialized)
        {
            for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg); i++)
            {
                clock_module_data.tick_reg[i].cb = NULL;
            }
            clock_module_data.clock_running = false;
            clock_module_data.current_ticks = 0;
            PRINT_MSG("\t%s Setting pause_clock to false\r\n", __FUNCTION__);
            clock_module_data.pause_clock = false;
            clock_module_data.kill_worker = false;
            assert(0 == pthread_create(&clock_module_data.worker_handle, NULL, sys_clock_worker, NULL));
            clock_module_data.initialized = true;
        }
        MUTEX_RELEASE();
    }
}


/**
 * @brief Function that resets the simply thread clock logic
 */
void simply_thead_system_clock_reset(void)
{
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    if(false != clock_module_data.initialized)
    {
        //Kill the worker thread
        clock_module_data.kill_worker = true;
        MUTEX_RELEASE();
        pthread_join(clock_module_data.worker_handle, NULL);
        MUTEX_GET();
    }
    clock_module_data.initialized = false;
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
    bool wait = true;
    init_if_needed();
    rv = NULL;
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    do
    {
        MUTEX_GET();
        if(false == clock_module_data.pause_clock)
        {
            clock_module_data.pause_clock = true;
            wait = false;
        }
        else
        {
            wait = true;
        }
        MUTEX_RELEASE();
    }
    while(wait == true);
    assert(true == clock_module_data.pause_clock);
    while(true == clock_module_data.clock_running) {} //Wait for the clock not to be running
    assert(false == clock_module_data.clock_running);
    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg) && NULL == rv; i++)
    {
        if(NULL == clock_module_data.tick_reg[i].cb)
        {
            clock_module_data.tick_reg[i].cb = on_tick;
            clock_module_data.tick_reg[i].args = args;
            rv = &clock_module_data.tick_reg[i];
        }
    }
    PRINT_MSG("\t%s Setting pause_clock to false\r\n", __FUNCTION__);
    clock_module_data.pause_clock = false; //Tell the clock to resume
    assert(NULL != rv);
    return rv;
}

/**
 * @brief Function the deregisters a function on tick
 * @param handle the handle to deregister
 */
void simply_thead_system_clock_deregister_on_tick(sys_clock_on_tick_handle_t handle)
{
    bool wait = true;
    struct sysclock_on_tick_registry_element_s *typed;
    typed = handle;
    assert(NULL != typed);
    PRINT_MSG("Running %s\r\n", __FUNCTION__);
    do
    {
        MUTEX_GET();
        if(false == clock_module_data.pause_clock)
        {
            clock_module_data.pause_clock = true;
            wait = false;
        }
        else
        {
            wait = true;
        }
        MUTEX_RELEASE();
    }
    while(wait == true);
    assert(true == clock_module_data.pause_clock);
    while(true == clock_module_data.clock_running) {} //Wait for the clock not to be running
    typed->cb = NULL;
    typed->args = NULL;
    PRINT_MSG("\t%s Setting pause_clock to false\r\n", __FUNCTION__);
    clock_module_data.pause_clock = false;
}

/**
 * @brief Function the deregisters a function on tick
 * @param handle the handle to deregister
 */
//void simply_thead_system_clock_deregister_on_tick_from_locked(sys_clock_on_tick_handle_t handle)
//{
//    bool wait = true;
//    struct sysclock_on_tick_registry_element_s *typed;
//    typed = handle;
//    assert(NULL != typed);
//    PRINT_MSG("Running %s\r\n", __FUNCTION__);
//    if(false != clock_module_data.pause_clock)
//    {
//      MUTEX_RELEASE();
//      do
//      {
//          MUTEX_GET();
//          if(false == clock_module_data.pause_clock)
//          {
//              clock_module_data.pause_clock = true;
//              wait = false;
//          }
//          else
//          {
//              wait = true;
//          }
//          if(true == wait)
//          {
//              MUTEX_RELEASE();
//          }
//      }while(wait == true);
//    }
//    assert(true == master_mutex_locked());
//    assert(true == clock_module_data.pause_clock);
//
//    while(true == clock_module_data.clock_running) {} //Wait for the clock not to be running
//    typed->cb = NULL;
//    typed->args = NULL;
//    PRINT_MSG("\t%s Setting pause_clock to false\r\n", __FUNCTION__);
//    clock_module_data.pause_clock = false;
//}

/**
 * Function that tells if it is safe to interrupt a task.  Must be called from a locked context
 * @return true if safe.
 */
bool simply_thead_system_clock_safe_to_interrupt(void)
{
    assert(true == master_mutex_locked());
    if(clock_module_data.pause_clock == true)
    {
        return false;
    }
    return true;
}

/**
 * Function that calculates the tick value
 * @param ticks the ticks to add to the current ticks
 * @return the the ticks after the added ticks
 */
uint64_t simply_thread_system_clock_tick_in(uint64_t ticks)
{
    return clock_module_data.current_ticks + ticks;
}
