#!/bin/bash -x

set -e
rm -f build/*.vktrace

TRACER=/work/vulkan_trace/VulkanTools/dbuild_x64/vktrace/vktrace
REPLAYER=/work/vulkan_trace/VulkanTools/dbuild_x64/vktrace/vkreplay
export MESA_VK_ABORT_ON_DEVICE_LOSS=1
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/work/vulkan_trace/VulkanTools/dbuild_x64/vktrace

function run()
{
	pushd build
	echo
	echo "-- Running test $1 with options $2 --"
	echo
	$TRACER -o ${1}${3}.vktrace -p ./$1 -a "$2"
	mv *.vktrace ../traces/
	popd
	$REPLAYER -x TRUE -o traces/${1}${3}.vktrace
}

run vulkan_general "-V 0" "V0"
#run vulkan_general "-V 1" "V1"
#run vulkan_general "-V 2" "V2"
#run vulkan_general "-V 3" "V3"
run vulkan_memory_1 "" ""
run vulkan_memory_1_1 "" ""
run vulkan_thread_1 "" ""
run vulkan_thread_2 "" ""
run vulkan_copying_1 "-V 0" "V0"
run vulkan_copying_1 "-V 1" "V1"
run vulkan_copying_1 "-V 2" "V2"
run vulkan_copying_1 "-f 1" "f1"
run vulkan_copying_1 "-q 1" "q1"
run vulkan_copying_1 "-q 2" "q2"
run vulkan_copying_1 "-q 3" "q3"
run vulkan_copying_1 "-q 4" "q4"
run vulkan_copying_1 "-q 5" "q5"
run vulkan_copying_1 "-m 1" "m1"
run vulkan_copying_1 "-m 2" "m2"
run vulkan_copying_1 "-m 1 -q 5 -b 1000" "m1q5b1000"
run vulkan_copying_2 "" ""
run vulkan_copying_2 "-q 1" "q1"
run vulkan_copying_2 "-m 1 -c 5" "m1c1"
#run vulkan_copying_2 "-V 1" "V1" # vulkan 1.3 required
run vulkan_copying_3 "" ""
run vulkan_copying_3 "-c 1" "c1"
#run vulkan_as_1 "" ""
run vulkan_tool_1 "" ""
#run vulkan_mesh_1 "" ""
run vulkan_compute_1 "" ""
run vulkan_compute_2 "" ""
rm -f test.bin
run vulkan_compute_1 "-pc" "pipelinecache"
run vulkan_compute_1 "-pc -pcf test.bin" "pipelinecache2"
run vulkan_compute_1 "-pc -pcf test.bin" "pipelinecache3" # repeat with test.bin
run vulkan_pipelinecache_1 "" ""

echo
echo "SUCCESS!"
