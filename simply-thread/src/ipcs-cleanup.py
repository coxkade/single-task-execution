'''
Tool For cleaning up dangling IPC semaphores and message queues
'''

import argparse
import os
import sys
import subprocess
import time

def clean_data(data):
    '''
    Function that clears out blank bits of the data
    '''
    rv = []
    for item in data:
        if '' != item:
            rv.append(item)
    return rv

def get_sem_queue_list():
    '''
    Function that gets lists of queues and semaphores to remove
    '''
    rv = {}
    rv["queues"] = []
    rv["sems"] = []
    result = subprocess.run("ipcs", shell=True, stdout=subprocess.PIPE)
    print("finished the run")
    result_string = result.stdout.decode('utf-8')
    print(result_string)
    lines = result_string.splitlines()
    for line in lines:
        work_data = line.split(' ')
        work_data = clean_data(work_data)
        if 3 <= len(work_data):
            if '0x00000000' == work_data[2]:
                if 'q' == work_data[0]:
                    rv["queues"].append(work_data[1])
                if 's' == work_data[0]:
                    rv["sems"].append(work_data[1])
    return rv

def local_string_append(one, two):
    '''
    Simple string append
    '''
    return "{}{}".format(one, two)

def remove_ipc_objects(onj_list):
    '''
    Function that actually removes the objects
    '''
    arguments=""
    for q in onj_list["queues"]:
        arguments = local_string_append(arguments, "-q {} ".format(q))
    for s in onj_list["sems"]:
        arguments = local_string_append(arguments, "-s {} ".format(s))
    if "" != arguments:
        print("Removing the objects")
        command = "ipcrm {}".format(arguments)
        print(command)
        subprocess.run(command, shell=True, check=True)

def main():
    '''
    The Main Function
    '''
    clear_data = get_sem_queue_list()
    remove_ipc_objects(clear_data)

if __name__ == "__main__":
    # execute only if run as a script
    main()