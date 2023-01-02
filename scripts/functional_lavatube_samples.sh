#!/bin/bash

REPORTDIR=reports/lavatube/samples

mkdir -p traces
mkdir -p $REPORTDIR
rm -f traces/sample_*.vk
rm -f $REPORTDIR/*.png
rm -f $REPORTDIR/*.html

REPORT=$REPORTDIR/report.html
HTMLIMGOPTS="width=200 height=200"
LAVATUBE_PATH=/work/lava/build

unset VK_INSTANCE_LAYERS
unset VK_LAYER_PATH
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-samples with lavatube</h1><table><tr><th>Name</th><th>Original</th><th>Replay original swapchain</th><th>Replay virtual swapchain</th></tr>" >> $REPORT

function run
{
	if [ -f traces/sample_$1.vk ]; then
		echo "Skipping sample_$1"
		return
	fi

	echo
	echo "****** $1 ******"
	echo

	echo
	echo "** native $1 **"
	echo

	rm -f external/vulkan-samples/*.ppm

	# Native run
	rm -f external/vulkan-samples/*.ppm
	( cd external/vulkan-samples ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 build/linux/app/bin/Debug/x86_64/vulkan_samples --benchmark --stop-after-frame=10 --force-close sample $1 )
	convert -alpha off external/vulkan-samples/3.ppm $REPORTDIR/sample_$1_f3_native.png
	rm -f external/vulkan-samples/*.ppm

	echo
	echo "** trace $1 **"
	echo

	# Make trace
	export LAVATUBE_DESTINATION=sample_$1
	export VK_LAYER_PATH=$LAVATUBE_PATH/implicit_layer.d
	export LD_LIBRARY_PATH=$LAVATUBE_PATH/implicit_layer.d
	export VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube
	( cd external/vulkan-samples ; build/linux/app/bin/Debug/x86_64/vulkan_samples --benchmark --stop-after-frame=100 --force-close sample $1 )
	mv external/vulkan-samples/sample_$1.vk traces/

	echo
	echo "** replay $1 **"
	echo

	# Replay
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $LAVATUBE_PATH/replay traces/sample_$1.vk
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay.png
	rm -f *.ppm
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay.png $REPORTDIR/sample_$1_f3_compare.png || true

	echo
	echo "** replay $1 virtual **"
	echo

	# Replay
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $LAVATUBE_PATH/replay -v traces/sample_$1.vk
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_virtual.png
	rm -f *.ppm
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_virtual.png $REPORTDIR/sample_$1_f3_compare_virtual.png || true

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay.png" /><img $HTMLIMGOPTS src="sample_$1_f3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_virtual.png" /><img $HTMLIMGOPTS src="sample_$1_f3_compare_virtual.png" /></td></tr>" >> $REPORT
}

run hello_triangle
run texture_loading
run compute_nbody
run dynamic_uniform_buffers
run hdr
run instancing
run separate_image_sampler
run terrain_tessellation
run texture_mipmap_generation
#run buffer_device_address
( vulkaninfo | grep -e VK_EXT_conservative_rasterization > /dev/null ) && run conservative_rasterization
run debug_utils
run descriptor_indexing
run dynamic_rendering
( vulkaninfo | grep -e VK_KHR_fragment_shading_rate > /dev/null ) && run fragment_shading_rate
( vulkaninfo | grep -e VK_KHR_fragment_shading_rate > /dev/null ) && run fragment_shading_rate_dynamic
#run open_gl_interop
run portability
run push_descriptors
#run ray_queries
#run ray_tracing_reflection
#run raytracing_basic
#run raytracing_extended
run synchronization_2
#run timeline_semaphore
run 16bit_arithmetic
run 16bit_storage_input_output
run afbc
run async_compute
run command_buffer_usage
run constant_data
run descriptor_management
run layout_transitions
run msaa
run multi_draw_indirect
run multithreading_render_passes
run pipeline_barriers
run pipeline_cache
run render_passes
run specialization_constants
run subpasses
run surface_rotation
run swapchain_images
run texture_compression_basisu
run texture_compression_comparison
run wait_idle
run profiles

echo "</table></body></html>" >> $REPORT
