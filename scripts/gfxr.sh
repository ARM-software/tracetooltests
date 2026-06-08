#!/bin/bash -e

rm -f build/*.gfxr

TRACEDIR=$(pwd)/traces_page_guard
ROOTDIR=/work/gfxr-internal/build
TRACER=${ROOTDIR}/layer
REPLAYER=${ROOTDIR}/tools/replay/gfxrecon-replay
CONVERT=${ROOTDIR}/tools/convert/gfxrecon-convert
export GFXRECON_MEMORY_TRACKING_MODE=page_guard

export MESA_VK_ABORT_ON_DEVICE_LOSS=1

mkdir -p ${TRACEDIR}

function run()
{
	pushd build
	echo
	echo "-- Running test $1 with options $2 --"
	echo
	LD_LIBRARY_PATH=${TRACER} VK_LAYER_PATH=${TRACER} VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct GFXRECON_CAPTURE_FILE=${1}${3}.gfxr ./$1 $2
	mv *.gfxr ${TRACEDIR}/${1}${3}.gfxr
	popd
	$REPLAYER ${TRACEDIR}/${1}${3}.gfxr
	$CONVERT --expand-flags --output stdout --format jsonl ${TRACEDIR}/${1}${3}.gfxr | jq -c 'del(.index)' > ${TRACEDIR}/${1}${3}.jsonl
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
run vulkan_copying_1 "-m 1" "m1"
run vulkan_copying_1 "-m 2" "m2"
run vulkan_copying_2 "-q 1" "q1"
run vulkan_copying_3 "" ""
run vulkan_copying_3 "-c 1" "c1"
#run vulkan_copying_3 "-c 2" "c2" # accepted failure, see https://github.com/LunarG/gfxreconstruct/issues/686
run vulkan_as_1 ""
#run vulkan_mesh_1 "" ""
run vulkan_compute_1 "" ""
run vulkan_compute_1 "-pc" "pipelinecache"
run vulkan_compute_1 "-pc -pcf test.bin" "pipelinecache2"
run vulkan_compute_1 "-pc -pcf test.bin" "pipelinecache3"
run vulkan_pipelinecache_1 "" ""
run vulkan_raytracing_1 "" ""
run vulkan_raytracing_2 "" ""
#run vulkan_raytracing_3 "" ""
run vulkan_raytracing_4 "" ""
run vulkan_raytracing_5 "" ""
run vulkan_raytracing_indirect_noop "" ""
run vulkan_demo_bloom_minimal "" ""

echo
echo "SUCCESS!"
