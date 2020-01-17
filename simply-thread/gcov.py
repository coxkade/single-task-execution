'''
Module that helps with generating the gcov output
'''

import argparse
import sys
import os
import subprocess

class Logger:
    def __init__(self, on = False):
        self.on = on
    def print(self, msg):
        if True == self.on:
            print(msg)

def find_file(pattern):
    '''
    Get the root directory of the file
    '''
    for root, dirs, files in os.walk(os.getcwd()):
        for file in files:
            total = os.path.join(root, file)
            if total.endswith(pattern):
                return os.path.join(root, file)
    return None

def generate(gcov, source, logger):
    '''
    Run Gcov for source
    '''
    logger.print("Generating source for {}".format(source))
    gcno = find_file("{}.gcno".format(source))
    gcda = find_file("{}.gcda".format(source))
    command = "{} {} -gcno={} -gcda={}".format(gcov, source, "{}".format(gcno), "{}".format(gcda))
    logger.print("Running Command:\n\t{}".format(command))
    subprocess.run(command, shell=True)

def clean(logger):
    '''
    Clear out all the gcno files
    '''
    file_list = []
    logger.print(os.getcwd())
    logger.print("Clear out all the gcno files")
    for root, dirs, files in os.walk(os.getcwd()):
        for file in files:
            if file.endswith(".gcno"):
                file_list.append(os.path.join(root, file))
    for file in file_list:
        logger.print("Removing file: {}".format(file))
        os.remove(file)

def main():
    '''
    The Main Function
    '''
    parser = argparse.ArgumentParser(description='Help cmake with gcov stuff')
    r_group = parser.add_argument_group('clean')
    r_group.add_argument('--clean', action='store_true', help='clean all the .gcno files')
    w_group = parser.add_argument_group('generate')
    w_group.add_argument("--gcov", help="The gcov executable to use")
    w_group.add_argument("--source", help="The Source file to generate the coverage for")
    parser.add_argument("--verbose", action='store_true', help="Print debug messages")
    args = parser.parse_args()
    
    logger = Logger(args.verbose)

    if True == args.clean:
        clean(logger)
    elif args.gcov and args.source:
        generate(args.gcov, args.source, logger)

if __name__ == '__main__':
    main()