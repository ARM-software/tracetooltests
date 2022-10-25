#!/bin/bash -e

rm -f build/*.gfxr

ROOTDIR=/work/gfxreconstruct/build
TRACER=${ROOTDIR}/layer
REPLAYER=${ROOTDIR}/tools/replay/gfxrecon-replay
export MESA_VK_ABORT_ON_DEVICE_LOSS=1
#GFXRECON_MEMORY_TRACKING_MODE=

function run()
{
	pushd build
	echo
	echo "-- Running test $1 with options $2 --"
	echo
	VK_LAYER_PATH=$TRACER VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct GFXRECON_CAPTURE_FILE=${1}${3}.gfxr ./$1 $2
	mv *.gfxr ../traces/${1}${3}.gfxr
	popd
	$REPLAYER traces/${1}${3}.gfxr
}

run vulkan_tool_1 ""
run vulkan_memory_1 "" ""
run vulkan_memory_1_1 "" ""
run vulkan_thread_1 "" ""
run vulkan_thread_2 "" ""
run vulkan_general "-V 0" "V0"
run vulkan_general "-V 1" "V1"
run vulkan_general "-V 2" "V2"
run vulkan_general "-V 3" "V3"
run vulkan_copying_1 "" ""
run vulkan_copying_1 "-f 1" "f1"
run vulkan_copying_1 "-q 1" "q1"
run vulkan_copying_1 "-q 2" "q2"
run vulkan_copying_1 "-q 3" "q3"
run vulkan_copying_1 "-q 4" "q4"
#run vulkan_copying_1 "-q 5" "q5" # two queues required
run vulkan_copying_1 "-m 1" "m1"
run vulkan_copying_1 "-m 2" "m2"
#run vulkan_copying_1 "-m 1 -q 5 -b 1000" "m1q5b1000" # two queues required
#run vulkan_copying_2 "" "" # two queues required
run vulkan_copying_2 "-q 1" "q1"
#run vulkan_copying_2 "-m 1 -c 5" "m1c1" # two queues required
#run vulkan_copying_2 "-V 1" "V1" # vulkan 1.3 and two queues required
run vulkan_copying_3 "" ""
run vulkan_copying_3 "-c 1" "c1"
#run vulkan_copying_3 "-c 2" "c2" # accepted failure, see https://github.com/LunarG/gfxreconstruct/issues/686
#run vulkan_as_1 ""
#run vulkan_mesh_1 "" ""
run vulkan_compute_1 "" ""
#run vulkan_compute_2 "" "" # two queues required
run vulkan_compute_1 "-pc" "pipelinecache"
run vulkan_compute_1 "-pc -pcf test.bin" "pipelinecache2"
run vulkan_compute_1 "-pc -pcf test.bin" "pipelinecache3"
run vulkan_pipelinecache_1 "" ""

echo
echo "SUCCESS!"
