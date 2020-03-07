/*
 * Task-Helper-Tests.c
 *
 *  Created on: Mar 7, 2020
 *      Author: kade
 */

#include <Task-Helper-Tests.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <Message-Helper.h>

//Macro that gets the number of elements supported by the array
#define ARRAY_MAX_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

/*********************************************************************
 ************** Message Helper Test Items ****************************
 ********************************************************************/

static void basic_message_helper_test(void **state)
{

}



/**
 * @brief run the task helper tests
 */
int run_task_helper_tests(void)
{
	int rv;
    const struct CMUnitTest message_helper_tests[] =
    {
        cmocka_unit_test(basic_message_helper_test)
    };
    rv = cmocka_run_group_tests(message_helper_tests, NULL, NULL);
    printf("rv: %i\r\n", rv);
    assert(0 <= rv);
    return 0;
}
