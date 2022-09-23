#!/bin/bash -e

set -x

mkdir -p traces
rm -f traces/uvkc_*.vktrace

unset VK_INSTANCE_LAYERS
unset VK_LAYER_PATH
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

function run1
{
	TESTNAME=$(echo $3 | tr '/' '_' | tr '=' '_')

	echo
	echo "****** $1_$2_$TESTNAME ******"
	echo

	echo
	echo "** trace $1_$2_$TESTNAME **"
	echo

	# Make trace
	DESTINATION=uvkc_$1_$2_$TESTNAME.vktrace
	( cd external/uvkcompute/build ; vktrace -o $DESTINATION -p ./benchmarks/$1/$2 -a "--benchmark_filter=$3" )
	mv external/uvkcompute/build/$DESTINATION traces/

	echo
	echo "** replay $1_$2_$TESTNAME original **"
	echo

	# Replay
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	# on desktop this crashes when replay is complete:
	#vkreplay -o traces/uvkc_$1_$2_$TESTNAME.vktrace
}

# single test traces
run1 "memory" "copy_storage_buffer" "scalar/1048576"
run1 "memory" "copy_storage_buffer" "vector/1048576"
run1 "memory" "copy_storage_buffer" "scalar/2097152"
run1 "memory" "copy_storage_buffer" "vector/2097152"
run1 "memory" "copy_storage_buffer" "scalar/4194304"
run1 "memory" "copy_storage_buffer" "vector/4194304"
run1 "memory" "copy_storage_buffer" "scalar/8388608"
run1 "memory" "copy_storage_buffer" "vector/8388608"
run1 "memory" "copy_storage_buffer" "scalar/16777216"
run1 "memory" "copy_storage_buffer" "vector/16777216"
run1 "memory" "copy_storage_buffer" "scalar/33554432"
run1 "memory" "copy_storage_buffer" "vector/33554432"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/1024x1024"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/1024x2048"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/1024x4096"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/2048x1024"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/2048x2048"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/2048x4096"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/4096x1024"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/4096x2048"
run1 "memory" "copy_sampled_image_to_storage_buffer" "ImageLoad/4096x4096"
run1 "compute" "mad_throughput" "f32/1048576/100000"
run1 "compute" "mad_throughput" "f32/1048576/200000"
run1 "compute" "mad_throughput" "f16/1048576/100000"
run1 "compute" "mad_throughput" "f16/1048576/200000"
run1 "reduction" "tree_reduce" "1048576xf32/loop/batch=16"
run1 "reduction" "tree_reduce" "1048576xf32/loop/batch=32"
run1 "reduction" "tree_reduce" "16777216xf32/loop/batch=64"
run1 "reduction" "tree_reduce" "2097152xf32/loop/batch=128"
run1 "reduction" "tree_reduce" "1048576xf32/subgroup/batch=16"
run1 "reduction" "tree_reduce" "1048576xf32/subgroup/batch=32"
run1 "reduction" "tree_reduce" "16777216xf32/subgroup/batch=64"
run1 "reduction" "tree_reduce" "2097152xf32/subgroup/batch=128"
run1 "reduction" "tree_reduce" "1048576xi32/loop/batch=16"
run1 "reduction" "tree_reduce" "1048576xi32/loop/batch=32"
run1 "reduction" "tree_reduce" "16777216xi32/loop/batch=64"
run1 "reduction" "tree_reduce" "2097152xi32/loop/batch=128"
run1 "reduction" "tree_reduce" "1048576xi32/subgroup/batch=16"
run1 "reduction" "tree_reduce" "1048576xi32/subgroup/batch=32"
run1 "reduction" "tree_reduce" "16777216xi32/subgroup/batch=64"
run1 "reduction" "tree_reduce" "2097152xi32/subgroup/batch=128"
run1 "reduction" "atomic_reduce" "f32/loop/batch=16"
run1 "reduction" "atomic_reduce" "f32/loop/batch=32"
run1 "reduction" "atomic_reduce" "f32/loop/batch=64"
run1 "reduction" "atomic_reduce" "f32/loop/batch=128"
run1 "reduction" "atomic_reduce" "f32/loop/batch=256"
run1 "reduction" "atomic_reduce" "f32/loop/batch=512"
run1 "reduction" "atomic_reduce" "f32/subgroup/batch=64"
run1 "reduction" "atomic_reduce" "f32/subgroup/batch=128"
run1 "reduction" "atomic_reduce" "f32/subgroup/batch=256"
run1 "reduction" "atomic_reduce" "f32/subgroup/batch=512"
run1 "reduction" "atomic_reduce" "i32/loop/batch=16"
run1 "reduction" "atomic_reduce" "i32/loop/batch=32"
run1 "reduction" "atomic_reduce" "i32/loop/batch=64"
run1 "reduction" "atomic_reduce" "i32/loop/batch=128"
run1 "reduction" "atomic_reduce" "i32/loop/batch=256"
run1 "reduction" "atomic_reduce" "i32/loop/batch=512"
run1 "reduction" "atomic_reduce" "i32/subgroup/batch=64"
run1 "reduction" "atomic_reduce" "i32/subgroup/batch=128"
run1 "reduction" "atomic_reduce" "i32/subgroup/batch=256"
run1 "reduction" "atomic_reduce" "i32/subgroup/batch=512"
run1 "reduction" "one_workgroup_reduce" "elements=1024/workgroup_size=16/loop"
run1 "reduction" "one_workgroup_reduce" "elements=4096/workgroup_size=16/loop"
run1 "reduction" "one_workgroup_reduce" "elements=16384/workgroup_size=16/loop"
run1 "reduction" "one_workgroup_reduce" "elements=65536/workgroup_size=16/loop"
run1 "reduction" "one_workgroup_reduce" "elements=1024/workgroup_size=16/subgroup"
run1 "reduction" "one_workgroup_reduce" "elements=4096/workgroup_size=16/subgroup"
run1 "reduction" "one_workgroup_reduce" "elements=16384/workgroup_size=16/subgroup"
run1 "reduction" "one_workgroup_reduce" "elements=65536/workgroup_size=16/subgroup"
run1 "reduction" "one_workgroup_reduce" "elements=1024/workgroup_size=16/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=4096/workgroup_size=16/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=16384/workgroup_size=16/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=65536/workgroup_size=16/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=1024/workgroup_size=32/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=4096/workgroup_size=32/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=16384/workgroup_size=32/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=65536/workgroup_size=32/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=1024/workgroup_size=64/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=4096/workgroup_size=64/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=16384/workgroup_size=64/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=65536/workgroup_size=64/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=1024/workgroup_size=128/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=4096/workgroup_size=128/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=16384/workgroup_size=128/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=65536/workgroup_size=128/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=1024/workgroup_size=256/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=4096/workgroup_size=256/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=16384/workgroup_size=256/atomic"
run1 "reduction" "one_workgroup_reduce" "elements=65536/workgroup_size=256/atomic"
run1 "subgroup" "subgroup_arithmetic" "add/loop"
run1 "subgroup" "subgroup_arithmetic" "add/intrinsic"
run1 "subgroup" "subgroup_arithmetic" "mul/loop"
run1 "subgroup" "subgroup_arithmetic" "mul/intrinsic"
run1 "overhead" "dispatch_void_shader" "dispatch"
