/*
 * Task-Helper.c
 *
 *  Created on: Mar 6, 2020
 *      Author: kade
 */

#include <Task-Helper.h>
#include <simply-thread-sem-helper.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

typedef void * (*TaskHelperWorker)(void * args) //!< Typedef for worker function to use with the task helper

typedef struct TaskHelperTask_t
{
	simply_thread_sem_t block_sem;
	pthread_t id;
}TaskHelperTask_t; //!< Instance data for each task helper task


TaskHelperTask_t * Task_Helper_Create_Task(TaskHelperWorker worker, void* args);

bool Task_Helper_Suspend_Task(TaskHelperTask_t * task);

bool Task_Helper_Resume_Task(TaskHelperTask_t * task);

void Task_Helper_Destroy_Task(TaskHelperTask_t * task);
