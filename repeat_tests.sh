#!/bin/bash
COMMAND="make semaphore-clean &&  python3 ~/workspace/Random_Python_Tools/run_ex.py 10 build/simply-thread/simply-thread-test-ex"

echo "Remove this file before merge"

make rm_build
make config
make -C build/ -j12 all

make semaphore-clean
python3 ~/workspace/Random_Python_Tools/ipcs-cleanup.py 
clear
python3 ~/workspace/Random_Python_Tools/run_ex.py 1000 build/simply-thread/simply-thread-test-ex
make semaphore-clean
