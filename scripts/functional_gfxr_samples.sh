#!/bin/bash

REPORTDIR=reports/gfxr/samples

mkdir -p traces
mkdir -p $REPORTDIR
rm -f external/vulkan-samples/*.ppm *.ppm traces/sample_*.gfxr

unset VK_INSTANCE_LAYERS
unset VK_LAYER_PATH

REPORT=$REPORTDIR/report.html
HTMLIMGOPTS="width=200 height=200"
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-samples with gfxreconstruct</h1><table><tr><th>Name</th><th>Original</th><th>Replay</th><th>Replay -m remap</th><th>Replay -m realign</th><th>Replay -m rebind</th></tr>" >> $REPORT

function run
{
	echo
	echo "****** $1 ******"
	echo

	echo
	echo "** native **"
	echo


	# Native run
	( cd external/vulkan-samples ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 build/linux/app/bin/Debug/x86_64/vulkan_samples --benchmark --stop-after-frame=10 --force-close sample $1 )
	convert -alpha off external/vulkan-samples/3.ppm $REPORTDIR/sample_$1_f3_native.png
	rm external/vulkan-samples/*.ppm

	echo
	echo "** trace **"
	echo

	# Make trace
	rm -f external/vulkan-samples/*.gfxr
	( cd external/vulkan-samples ; gfxrecon-capture.py -o sample_$1.gfxr build/linux/app/bin/Debug/x86_64/vulkan_samples --benchmark --stop-after-frame=10 --force-close sample $1 )
	mv external/vulkan-samples/sample_$1*.gfxr traces/sample_$1.gfxr

	echo
	echo "** replay **"
	echo


	# Replay
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 gfxrecon-replay traces/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay.png $REPORTDIR/sample_$1_f3_compare.png || true
	rm -f *.ppm

	# Replay -m remap
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 gfxrecon-replay -m remap traces/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_remap.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_remap.png $REPORTDIR/sample_$1_f3_compare_remap.png || true
	rm -f *.ppm

	# Replay -m realign
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 gfxrecon-replay -m realigntraces/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_realign.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_realign.png $REPORTDIR/sample_$1_f3_compare_realign.png || true
	rm -f *.ppm

	# Replay -m rebind
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 gfxrecon-replay -m rebind traces/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_rebind.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_rebind.png $REPORTDIR/sample_$1_f3_compare_rebind.png || true
	rm -f *.ppm

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay.png" /><img $HTMLIMGOPTS src="sample_$1_f3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_remap.png" /><img $HTMLIMGOPTS src="sample_$1_f3_compare_remap.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_realign.png" /><img $HTMLIMGOPTS src="sample_$1_f3_compare_realign.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_rebind.png" /><img $HTMLIMGOPTS src="sample_$1_f3_compare_rebind.png" /></td>" >> $REPORT
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
run buffer_device_address
run conservative_rasterization
run debug_utils
run descriptor_indexing
#run dynamic_rendering
#run fragment_shading_rate
#run fragment_shading_rate_dynamic
#run open_gl_interop # hangs
#run portability
run push_descriptors
#run ray_queries
#run ray_tracing_reflection
#run raytracing_basic
#run raytracing_extended
#run synchronization_2
run timeline_semaphore
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
