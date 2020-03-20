/**
 * @file Thread-Helper.c
 * @author Kade Cox
 * @date Created: Mar 9, 2020
 * @details
 *
 */

#include <Thread-Helper.h>
#include <simply-thread.h>
#include <simply-thread-log.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/***********************************************************************************/
/***************************** Defines and Macros **********************************/
/***********************************************************************************/

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifdef DEBUG_THREAD_HELPER
#define PRINT_MSG(...) simply_thread_log(COLOR_MAGENTA, __VA_ARGS__)
#define PRINT_COLOR_MSG(C, ...) simply_thread_log(C, __VA_ARGS__)
#else
#define PRINT_MSG(...)
#define PRINT_COLOR_MSG(C, ...)
#endif //DEBUG_THREAD_HELPER

#ifndef MAX_THREAD_COUNT
#define MAX_THREAD_COUNT 100
#endif //MAX_THREAD_COUNT

/***********************************************************************************/
/***************************** Type Defs *******************************************/
/***********************************************************************************/

struct thread_helper_module_data_s
{
    bool signals_initialized;
    struct sigaction pause_action;
    struct sigaction kill_action;
    struct{
    	helper_thread_t thread;
    	bool in_use;
    }registry[MAX_THREAD_COUNT];
	pthread_t id;
	int queue_id;
	bool kill_worker;
};

struct self_fetch_data_s
{
    helper_thread_t *result;
    pthread_t id;
}; //!<Structure used for the self fetch data

struct thread_helper_message_data_s
{
	enum{
		TH_THREAD_CREATE,
		TH_THREAD_DESTROY,
		TH_THREAD_RESET,
		TH_THREAD_SELF,
		TH_ENTER_CRIT,
		TH_EXIT_CRIT,
		TH_THREAD_PAUSE,
		TH_THREAD_RESUME,
		TH_THREAD_KILL,
	}action;
	union{
		struct{
			helper_thread_t ** new_thread;
			void *(* worker)(void *);
			void * data;
		}create; //!< Data for the create request
		struct{
			helper_thread_t * thread;
			bool * result;
			bool resp_crit;
		}destroy; //!< Data for the destroy request
		 struct{
			helper_thread_t ** thread;
			pthread_t id;
		}self; //!< Data for fetching the current thread
		struct{
			helper_thread_t * thread;
		}crit; //!< Data for working with entering and exiting critical sections
		struct{
			helper_thread_t * thread;
		}pause; //!< Data for pausing and unpausing threads
	}data;
	bool * finished;
};

union thread_helper_union
{
    int val;
    struct semid_ds *buf;
    unsigned short  *array;
};

struct thread_helper_raw_message_s
{
    long type;
    union{
    char msg[sizeof(struct thread_helper_message_data_s)];
    struct thread_helper_message_data_s decoded;
    };
};

/***********************************************************************************/
/***************************** Function Declarations *******************************/
/***********************************************************************************/

/**
 * @brief Function for catching the pause signal
 * @param signo
 */
static void m_catch_pause(int signo);

/**
 * @brief Function that catches the kill message
 * @param signo
 */
static void m_catch_kill(int signo);

/***********************************************************************************/
/***************************** Static Variables ************************************/
/***********************************************************************************/

static struct thread_helper_module_data_s thread_helper_data =
{
    .signals_initialized = false,
	.queue_id = -1
}; //!<< This modules local data



/***********************************************************************************/
/***************************** Function Definitions ********************************/
/***********************************************************************************/


/**
 * Initialize the ipc message queue
 */
static inline int init_msg_queue(void)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
    int error_number;
    int result;
    result = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    if(0  > result)
    {
        error_number = errno;
        ST_LOG_ERROR("%s %s msgget error %i\r\n", __FILE__, __FUNCTION__, error_number);
    }
    SS_ASSERT(0 <= result);
    return result;
}

/**
 * The internal thread runner
 * @param data
 */
static void* thread_helper_runner(void * data)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	helper_thread_t * typed;
	typed = data;
	SS_ASSERT(NULL != typed);
	typed->thread_running = true;
	return typed->worker(typed->worker_data);
}

/**
 * Internal function that sends out a message
 * @param msg
 */
static void thread_helper_send_message(struct thread_helper_raw_message_s * msg)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	int result;
	result = msgsnd(thread_helper_data.queue_id, msg, sizeof(msg->decoded), 0);
	SS_ASSERT(0 == result);
}

/**
 * @brief internal function that creates a new thread
 * @param message
 */
static void internal_thread_create(struct thread_helper_message_data_s * message)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL == message->data.create.new_thread[0]);
	//first we need to find a registery entry
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry) && NULL == message->data.create.new_thread[0]; i++)
	{
		if(false == thread_helper_data.registry[i].in_use)
		{
			thread_helper_data.registry[i].in_use = true;
			message->data.create.new_thread[0] = &thread_helper_data.registry[i].thread;
			message->data.create.new_thread[0]->worker = message->data.create.worker;
			message->data.create.new_thread[0]->worker_data = message->data.create.data;
			message->data.create.new_thread[0]->thread_running = false;
			message->data.create.new_thread[0]->in_crit_section = false;
			Sem_Helper_sem_init(&message->data.create.new_thread[0]->wait_sem);
			SS_ASSERT(0 == pthread_create(&message->data.create.new_thread[0]->id, NULL, thread_helper_runner, message->data.create.new_thread[0]));
			while(false ==message->data.create.new_thread[0]->thread_running );
		}
	}
}

/**
 * @brief destroy a thread by its pthread id
 * @param id
 */
static void internal_destroy_thread_id(helper_thread_t * thread)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	bool entry_found = false;
	SS_ASSERT(0 == pthread_kill(thread->id, KILL_SIGNAL));
	pthread_join(thread->id, NULL);
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry) && false == entry_found; i++)
	{
		if(thread == &thread_helper_data.registry[i].thread)
		{
			thread_helper_data.registry[i].in_use = false;
			entry_found = true;
		}
	}
	SS_ASSERT(true == entry_found);
}

/**
 * @brief the internal thread destroy
 * @param message
 * @return true if destroyed
 */
static bool internal_thread_destroy(struct thread_helper_message_data_s * message)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	if(true == message->data.destroy.resp_crit && true == message->data.destroy.thread->in_crit_section)
	{
		return false;
	}
	internal_destroy_thread_id(message->data.destroy.thread);
	message->data.destroy.result[0] = true;
	return true;
}

/**
 * handle the internal reset
 */
static void internal_reset(void)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry); i++)
	{
		if(true == thread_helper_data.registry[i].in_use)
		{
			internal_destroy_thread_id(&thread_helper_data.registry[i].thread);
		}
	}
}

/**
 * @brief find the self thread
 * @param message
 */
static void internal_thread_self(struct thread_helper_message_data_s * message)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL ==  message->data.self.thread[0]);
	for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry) && NULL == message->data.self.thread[0]; i++)
	{
		if(true == thread_helper_data.registry[i].in_use)
		{
			if(message->data.self.id == thread_helper_data.registry[i].thread.id)
			{
				message->data.self.thread[0] = &thread_helper_data.registry[i].thread;
			}
		}
	}
}

/**
 * @brief function for handling the entering of critical sections
 * @param message
 * @param enter
 * @return true on success
 */
static bool internal_thread_handle_crit(struct thread_helper_message_data_s * message, bool enter)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != message->data.crit.thread);
	if(enter == message->data.crit.thread->in_crit_section)
	{
		return false;
	}
	message->data.crit.thread->in_crit_section = enter;
	return true;
}

/**
 * @brief internal function for handling pause messages
 * @param message
 * @return
 */
static bool internal_thread_handle_pause(struct thread_helper_message_data_s * message)
{
	int result;
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != message->data.pause.thread);
	SS_ASSERT(true == message->data.pause.thread->thread_running);
	if(true == message->data.pause.thread->in_crit_section)
	{
		return false;
	}
	while(EAGAIN != Sem_Helper_sem_trywait(&message->data.pause.thread->wait_sem)) {} //Force the sem to block
	result = pthread_kill(message->data.pause.thread->id, PAUSE_SIGNAL);
	SS_ASSERT(0 == result);
	return true;
}

/**
 * @brief Handle a thread resume
 * @param message
 */
static void internal_thread_handle_resume(struct thread_helper_message_data_s * message)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != message->data.pause.thread);
	SS_ASSERT(false == message->data.pause.thread->thread_running);
	Sem_Helper_sem_post(&message->data.pause.thread->wait_sem);
	while(false == message->data.pause.thread->thread_running) {}
}

/**
 * The internal worker function that processes the internal queue data
 * @param data
 */
static void * thread_helper_worker(void * data)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	int result;
	struct thread_helper_raw_message_s in_message;

	SS_ASSERT(NULL == data);
	while(false == thread_helper_data.kill_worker)
	{
		result = msgrcv(thread_helper_data.queue_id, &in_message, sizeof(struct thread_helper_raw_message_s), 1, 0);
		 if(result != -1)
		 {
			 SS_ASSERT(1 == in_message.type);
			 PRINT_MSG("\t%s received message type %i\r\n", __FUNCTION__, in_message.decoded.action);
			 switch(in_message.decoded.action)
			 {
			 case TH_THREAD_CREATE:
				 //We need to create a thread
				 internal_thread_create(&in_message.decoded);
				 in_message.decoded.finished[0] = true;
				 break;
			 case TH_THREAD_DESTROY:
				 if(true ==  internal_thread_destroy(&in_message.decoded))
				 {
					 in_message.decoded.finished[0] = true;
				 }
				 else
				 {
					 thread_helper_send_message(&in_message);
				 }
				 break;
			 case TH_THREAD_RESET:
				 internal_reset();
				 in_message.decoded.finished[0] = true;
				 break;
			 case TH_THREAD_SELF:
				 internal_thread_self(&in_message.decoded);
				 in_message.decoded.finished[0] = true;
				 break;
			 case TH_THREAD_PAUSE:
				 if(true == internal_thread_handle_pause(&in_message.decoded))
				 {
					 in_message.decoded.finished[0] = true;
				 }
				 else
				 {
					 thread_helper_send_message(&in_message);
				 }
				 break;
			 case TH_EXIT_CRIT:
				 if(true == internal_thread_handle_crit(&in_message.decoded, false))
				 {
					 in_message.decoded.finished[0] = true;
				 }
				 else
				 {
					 thread_helper_send_message(&in_message);
				 }
				 break;
			 case TH_ENTER_CRIT:
				 if(true == internal_thread_handle_crit(&in_message.decoded, true))
				 {
					 in_message.decoded.finished[0] = true;
				 }
				 else
				 {
					 thread_helper_send_message(&in_message);
				 }
				 break;
			 case TH_THREAD_RESUME:
				 internal_thread_handle_resume(&in_message.decoded);
				 in_message.decoded.finished[0] = true;
				 break;
			 case TH_THREAD_KILL:
				 thread_helper_data.kill_worker = true;
				 in_message.decoded.finished[0] = true;
				 break;
			 default:
				 SS_ASSERT(true == false);
				 break;
			 }

		 }
		 else
		 {
			 ST_LOG_ERROR("%s error msgrcv failed with error %i\r\n", __FUNCTION__, errno);
			 SS_ASSERT(true == false);
		 }

	}
	if(msgctl(thread_helper_data.queue_id, IPC_RMID, NULL) == -1)
	{
		ST_LOG_ERROR("Failed to delete my message queue\r\n");
	}
	thread_helper_data.queue_id = -1;
	return NULL;
}

/**
 * @brief initialize this module if required
 */
static void init_if_needed(void)
{
    if(-1 == thread_helper_data.queue_id)
    {
    	PRINT_MSG("%s Running\r\n", __FUNCTION__);
        for(unsigned int i = 0; i < ARRAY_MAX_COUNT(thread_helper_data.registry); i++)
        {
            thread_helper_data.registry[i].in_use = false;
        }
        thread_helper_data.kill_worker = false;
        thread_helper_data.queue_id = init_msg_queue();
        SS_ASSERT(0 == pthread_create(&thread_helper_data.id, NULL, thread_helper_worker, NULL));
    }
    if(false == thread_helper_data.signals_initialized)
    {
        PRINT_MSG("%s Initializing the module\r\n", __FUNCTION__);
        thread_helper_data.pause_action.sa_handler = m_catch_pause;
        thread_helper_data.pause_action.sa_flags = 0;
        SS_ASSERT(0 == sigaction(PAUSE_SIGNAL, &thread_helper_data.pause_action, NULL));
        thread_helper_data.kill_action.sa_handler = m_catch_kill;
        thread_helper_data.kill_action.sa_flags = 0;
        SS_ASSERT(0 == sigaction(KILL_SIGNAL, &thread_helper_data.kill_action, NULL));
        thread_helper_data.signals_initialized = true;
    }

}

/**
 * @brief Function for catching the pause signal
 * @param signo
 */
static void m_catch_pause(int signo)
{
    PRINT_MSG("%s Started %p\r\n", __FUNCTION__, pthread_self());
    int result = -1;
    helper_thread_t *worker;
    sigset_t waiting_mask;
    SS_ASSERT(PAUSE_SIGNAL == signo);
    SS_ASSERT(0 == sigpending(&waiting_mask));
    if(sigismember(&waiting_mask, PAUSE_SIGNAL))
    {
        PRINT_MSG("\t%s PAUSE_SIGNAL is pending\r\n", __FUNCTION__);
    }
    PRINT_MSG("\t%s Fetching the worker\r\n", __FUNCTION__);
    worker = thread_helper_self();


    SS_ASSERT(NULL != worker);

    worker->thread_running = false;
    PRINT_MSG("\tThread No Longer Running\r\n");
    while(0 != result)
    {
        PRINT_MSG("\t%s waiting on sem %i\r\n", __FUNCTION__, worker->wait_sem.id);
        result = Sem_Helper_sem_wait(&worker->wait_sem);
        if(0 != result)
        {
            PRINT_COLOR_MSG(COLOR_PURPLE, "---m_catch_pause returned %i\r\n", result);
        }
    }
    worker->thread_running = true;
    PRINT_MSG("\tThread Now Running\r\n");
    PRINT_MSG("%s Finishing\r\n", __FUNCTION__);
}

/**
 * @brief Function that catches the kill message
 * @param signo
 */
static void m_catch_kill(int signo)
{
    PRINT_MSG("%s Started %p\r\n", __FUNCTION__, pthread_self());
    SS_ASSERT(KILL_SIGNAL == signo);
    PRINT_MSG("\t%s Calling pthread_exit\r\n", __FUNCTION__);
    pthread_exit(NULL);
}



/**
 * Send a message and wait for the action to complete
 * @param message
 */
static void send_and_wait(struct thread_helper_raw_message_s * message)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	thread_helper_send_message(message);
	while(false == message->decoded.finished[0]) {}
}

/**
 * @brief Function that fetches the current thread helper
 * @return NULL if not known
 */
helper_thread_t *thread_helper_self(void)
{
	struct thread_helper_raw_message_s message;
	helper_thread_t * result;
	bool comp = false;
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	init_if_needed();

	result = NULL;
	message.decoded.data.self.thread = &result;
	message.decoded.data.self.id = pthread_self();
	message.decoded.action = TH_THREAD_SELF;
	message.decoded.finished = &comp;
	message.type = 1;

	send_and_wait(&message);
	return result;
}

/**
 * @brief Create and start a new thread
 * @param worker Function used with the thread
 * @param data the data used with the thread
 * @return Ptr to the new thread object
 */
helper_thread_t *thread_helper_thread_create(void *(* worker)(void *), void *data)
{
	struct thread_helper_raw_message_s message;
	helper_thread_t * result;
	bool comp = false;

	SS_ASSERT(NULL != worker);
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	init_if_needed();
	result = NULL;
	message.decoded.finished = &comp;
	message.decoded.data.create.new_thread = &result;
	message.decoded.data.create.data = data;
	message.decoded.data.create.worker = worker;
	message.decoded.action = TH_THREAD_CREATE;
	message.type = 1;

	send_and_wait(&message);
	SS_ASSERT(NULL != result);
	return result;
}

/**
 * @brief Destroy a previously created thread.  This thread blocks
 * @param thread
 */
static void thread_helper_destroy(helper_thread_t *thread, bool respect_crit_section)
{
	struct thread_helper_raw_message_s message;
	bool result = false;
	bool comp = false;

	SS_ASSERT(NULL != thread);
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	init_if_needed();
	message.decoded.finished = &comp;
	message.decoded.data.destroy.result = &result;
	message.decoded.data.destroy.thread = thread;
	message.decoded.data.destroy.resp_crit = respect_crit_section;
	message.decoded.action = TH_THREAD_DESTROY;
	message.type = 1;

	send_and_wait(&message);
	SS_ASSERT(true == result);
}

/**
 * @brief Destroy a previously created thread.  This thread blocks
 * @param thread
 */
void thread_helper_thread_destroy(helper_thread_t *thread)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	thread_helper_destroy(thread, true);
}

/**
 * Function that destroys thread from the assert context
 * @param thread
 */
void thread_helper_thread_assert_destroy(helper_thread_t *thread)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	thread_helper_destroy(thread, false);
}

/**
 * @brief Check if a thread is running
 * @param thread The thread to check
 * @return True if the thread is running.  False otherwise.
 */
bool thread_helper_thread_running(helper_thread_t *thread)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != thread);
	return thread->thread_running;
}

/**
 * @brief Pause a thread
 * @param thread
 */
void thread_helper_pause_thread(helper_thread_t *thread)
{
	struct thread_helper_raw_message_s message;
	bool comp = false;

	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != thread);

	init_if_needed();
	message.decoded.finished = &comp;
	message.decoded.action = TH_THREAD_PAUSE;
	message.decoded.data.pause.thread = thread;
	message.type = 1;

	send_and_wait(&message);
	while(true == thread->thread_running) {}
}

/**
 * Have a thread enter a critical section
 */
void thread_enter_critical_section(helper_thread_t *thread)
{
	struct thread_helper_raw_message_s message;
	bool comp = false;
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != thread);

	init_if_needed();
	message.decoded.finished = &comp;
	message.decoded.action = TH_ENTER_CRIT;
	message.decoded.data.crit.thread = thread;
	message.type = 1;

	send_and_wait(&message);
}

/**
 * Have a thread enter a critical section
 */
void thread_exit_critical_section(helper_thread_t *thread)
{
	struct thread_helper_raw_message_s message;
	bool comp = false;
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != thread);

	init_if_needed();
	message.decoded.finished = &comp;
	message.decoded.action = TH_EXIT_CRIT;
	message.decoded.data.crit.thread = thread;
	message.type = 1;

	send_and_wait(&message);
}

/**
 * @brief Run a thread
 * @param thread
 */
void thread_helper_run_thread(helper_thread_t *thread)
{
	struct thread_helper_raw_message_s message;
	bool comp = false;

	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != thread);

	init_if_needed();
	message.decoded.finished = &comp;
	message.decoded.action = TH_THREAD_RESUME;
	message.decoded.data.pause.thread = thread;
	message.type = 1;

	send_and_wait(&message);

}

/**
 * @brief get the id of the worker thread
 * @param thread
 * @return the thread id
 */
pthread_t thread_helper_get_id(helper_thread_t *thread)
{
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	SS_ASSERT(NULL != thread);
	return thread->id;
}

/**
 * @brief Reset the thread helper
 */
void reset_thread_helper(void)
{
	struct thread_helper_raw_message_s message;
	bool comp = false;
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	init_if_needed();
	message.decoded.finished = &comp;
	message.decoded.action = TH_THREAD_RESET;
	message.type = 1;

	send_and_wait(&message);
}

/**
 * Function to call when program exits to clean up all the hanging thread helper data
 */
void thread_helper_cleanup(void)
{
	struct thread_helper_raw_message_s message;
	bool comp = false;
	PRINT_MSG("%s Running\r\n", __FUNCTION__);
	reset_thread_helper();
	message.decoded.finished = &comp;
	message.decoded.action = TH_THREAD_KILL;
	message.type = 1;
	send_and_wait(&message);
	pthread_join(thread_helper_data.id, NULL);
	SS_ASSERT(-1 == thread_helper_data.queue_id);
}
