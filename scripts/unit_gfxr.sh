#!/bin/bash

set -e
rm -f *.gfxr

function run()
{
	echo
	echo "-- Running test $1 with options $2 --"
	echo
	gfxrecon-capture.py -o ${1}${3}.gfxr ./$1 $2
	gfxrecon-replay ${1}${3}_*.gfxr
}

run vulkan_general "" ""
run vulkan_copying_1 "" ""
run vulkan_copying_1 "-f 1" "f1"
run vulkan_copying_1 "-q 1" "q1"
run vulkan_copying_1 "-q 2" "q2"
run vulkan_copying_1 "-q 3" "q3"
run vulkan_copying_1 "-q 4" "q4"
run vulkan_copying_1 "-q 5" "q5"
run vulkan_copying_1 "-m 1" "m1"
run vulkan_copying_1 "-m 2" "m2"
run vulkan_copying_2 "" ""
run vulkan_copying_2 "-q 1" "q1"
run vulkan_copying_2 "-f 1" "f1"
run vulkan_copying_3 "" ""
run vulkan_copying_3 "-c 1" "c1"
#run vulkan_copying_3 "-c 2" "c2" # crashes, see https://github.com/LunarG/gfxreconstruct/issues/686
run vulkan_memory_1 "" ""
run vulkan_thread_1 "" ""
run vulkan_thread_2 "" ""
#run vulkan_as_1 ""
#run vulkan_tool_1 ""

echo
echo "SUCCESS!"
