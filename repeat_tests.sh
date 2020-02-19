#!/bin/bash
COMMAND="make semaphore-clean &&  python3 ~/workspace/Random_Python_Tools/run_ex.py 10 build/simply-thread/simply-thread-test-ex"

echo "Remove this file before merge"

make rm_build
make config
make -C build/ -j12 all

# for i in {1..10}; do make semaphore-clean && build/simply-thread/simply-thread-test-ex ; done
make semaphore-clean
clear
python3 ~/workspace/Random_Python_Tools/run_ex.py 500 build/simply-thread/simply-thread-test-ex
make semaphore-clean