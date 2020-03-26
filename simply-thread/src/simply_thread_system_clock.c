/**
 * @file simply_thread_system_clock.c
 * @author Kade Cox
 * @date Created: Feb 28, 2020
 * @details
 * Module for the system clock for the simply thread library
 */

#include <simply_thread_system_clock.h>
#include <Thread-Helper.h>
#include <Sem-Helper.h>
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
    helper_thread_t *tick_thread;  //!< The tick worker thread
    bool cleaning_up; //!< Boolean value that says we are cleaning up
    bool tick_tock; //!Value that inverts back and forth on tick tock
}; //!< Local module data

struct system_clock_reg_data_s
{
    void (*on_tick)(sys_clock_on_tick_handle_t handle, uint64_t tickval, void *args);
    void *args;
    sys_clock_on_tick_handle_t rv;
}; //!< Structure for executing registry int the TCB context

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
    .tick_thread = NULL,
    .cleaning_up = false,
    .tick_tock = false
}; //!< Variable that holds the local modules data

/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


/**
 * @brief Function that toggles the ticktock values
 */
static inline void do_tick_tock(void)
{
    if(false == clock_module_data.tick_tock)
    {
        clock_module_data.tick_tock = true;
    }
    else
    {
        clock_module_data.tick_tock = false;
    }
}


/**
 * The Tick worker function
 * @param data
 */
static void *on_tick_worker(void *data)
{
    struct sysclock_on_tick_registry_element_s worker;
    helper_thread_t *me;

    me = thread_helper_self();
    SS_ASSERT(NULL != me);
    while(1)
    {
        simply_thread_sleep_ns(ST_NS_PER_MS);
        do_tick_tock();
        if(false == clock_module_data.cleaning_up)
        {
            clock_module_data.current_ticks++;
            for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg); i++)
            {
                memcpy(&worker, &clock_module_data.tick_reg[i], sizeof(worker));
                if(NULL != worker.cb && true == worker.enabled)
                {
                    //Call the callback here
                    worker.cb(&clock_module_data.tick_reg[i], clock_module_data.current_ticks, worker.args); //This can ask for things to be on the tcb
                }
            }
        }
    }
    return NULL;
}

/**
 * @brief Function that initializes the thread worker
 * @param data
 */
static void init_thread_worker(void *data)
{
    SS_ASSERT(NULL == data);
    if(NULL == clock_module_data.tick_thread)
    {
        clock_module_data.tick_thread = thread_helper_thread_create(on_tick_worker, NULL);
        SS_ASSERT(NULL != clock_module_data.tick_thread);
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg); i++)
        {
            clock_module_data.tick_reg[i].cb = NULL;
        }
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
        PRINT_MSG("%s Running in TCB Context\r\n", __FUNCTION__);
        run_in_tcb_context(init_thread_worker, NULL);
        PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
    }
}



/**
 * @brief Function that resets the simply thread clock logic
 */
void simply_thead_system_clock_reset(void)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    if(true == clock_module_data.cleaning_up)
    {
        while(true == clock_module_data.cleaning_up) {}
        return;
    }
    if(NULL != clock_module_data.tick_thread)
    {
        clock_module_data.cleaning_up = true;

        if(NULL != clock_module_data.tick_thread)
        {
            clock_module_data.tick_thread = NULL;
        }

        clock_module_data.cleaning_up = false;
    }
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief register an on tick handler in the TCB context
 * @param data
 */
static void stsc_reg_in_tcb(void *data)
{
    struct system_clock_reg_data_s *typed;
    struct sysclock_on_tick_registry_element_s worker;
    typed = data;
    SS_ASSERT(NULL != typed);

    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);

    worker.enabled = true;
    worker.cb = typed->on_tick;
    worker.args = typed->args;

    for(unsigned int i = 0; i < ARRAY_MAX_COUNT(clock_module_data.tick_reg) && NULL == typed->rv; i++)
    {
        if(NULL == clock_module_data.tick_reg[i].cb)
        {
            typed->rv = &clock_module_data.tick_reg[i];
            memcpy(&clock_module_data.tick_reg[i], &worker, sizeof(worker));
        }
    }
    PRINT_MSG("\t%s Succeeded\r\n", __FUNCTION__);
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
    struct system_clock_reg_data_s worker_data;

    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_required();
    worker_data.rv = NULL;
    SS_ASSERT(NULL != on_tick);
    worker_data.on_tick = on_tick;
    worker_data.args = args;
    run_in_tcb_context(stsc_reg_in_tcb, &worker_data);
    PRINT_MSG("%s Succeeded\r\n", __FUNCTION__);
    return worker_data.rv;
}

/**
 * @brief deregister from the task control block context
 * @param data
 */
static void stsc_dereg_in_tcb(void *data)
{
    struct sysclock_on_tick_registry_element_s worker;
    struct sysclock_on_tick_registry_element_s *typed;
    PRINT_MSG("\t%s Running\r\n", __FUNCTION__);
    typed = data;
    SS_ASSERT(NULL != typed);
    worker.cb = NULL;
    worker.args = NULL;
    worker.enabled = false;
    memcpy(typed, &worker, sizeof(worker));
    PRINT_MSG("\t%s Succeeded\r\n", __FUNCTION__);
}

/**
 * @brief Function the deregisters a function on tick
 * @param handle the handle to deregister
 */
void simply_thead_system_clock_deregister_on_tick(sys_clock_on_tick_handle_t handle)
{
    PRINT_MSG("%s Running\r\n", __FUNCTION__);
    init_if_required();
    run_in_tcb_context(stsc_dereg_in_tcb, handle);
    PRINT_MSG("%s Succeeded\r\n", __FUNCTION__);
}

/**
 * Function that calculates the tick value
 * @param ticks the ticks to add to the current ticks
 * @return the the ticks after the added ticks
 */
uint64_t simply_thread_system_clock_tick_in(uint64_t ticks)
{
    uint64_t result;
    init_if_required();
    result =  clock_module_data.current_ticks + ticks;
    return result;
}
