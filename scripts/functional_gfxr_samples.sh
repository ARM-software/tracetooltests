#!/bin/bash

if [ "$LAYERPATH" != "" ];
then
	REPLAYER="$LAYERPATH/gfxrecon-replay"
	TRACER="$LAYERPATH/gfxrecon-capture-vulkan.py --capture-layer $LAYERPATH"
fi

REPORTDIR=reports/gfxr/samples${TAG}
REPORT=$REPORTDIR/report.html
TRACEDIR=traces${TAG}
REPLAYER=${REPLAYER:-"$(which gfxrecon-replay)"}
TRACER=${TRACER:-"$(which gfxrecon-capture-vulkan.py)"}

rm -rf external/vulkan-samples/*.ppm *.ppm $TRACEDIR/sample_*.gfxr $REPORTDIR external/vulkan-demos/*.gfxr
mkdir -p $TRACEDIR
mkdir -p $REPORTDIR

unset VK_INSTANCE_LAYERS

HTMLIMGOPTS="width=200 height=200"
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-samples with gfxreconstruct</h1><table><tr><th>Name</th><th>Original</th><th>Replay -m none</th><th>Replay -m remap</th><th>Replay -m realign</th><th>Replay -m rebind</th><th>Replay trim</th></tr>" >> $REPORT

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
	( cd external/vulkan-samples ; ${TRACER} -o sample_$1.gfxr build/linux/app/bin/Debug/x86_64/vulkan_samples --benchmark --stop-after-frame=10 --force-close sample $1 )
	mv external/vulkan-samples/sample_$1*.gfxr $TRACEDIR/sample_$1.gfxr

	echo
	echo "** replay using $REPLAYER **"
	echo


	# Replay -m none
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m none $TRACEDIR/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay.png $REPORTDIR/sample_$1_f3_compare.png || true
	rm -f *.ppm

	# Replay -m remap
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m remap $TRACEDIR/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_remap.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_remap.png $REPORTDIR/sample_$1_f3_compare_remap.png || true
	rm -f *.ppm

	# Replay -m realign
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m realign $TRACEDIR/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_realign.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_realign.png $REPORTDIR/sample_$1_f3_compare_realign.png || true
	rm -f *.ppm

	# Replay -m rebind
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m rebind $TRACEDIR/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_rebind.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_rebind.png $REPORTDIR/sample_$1_f3_compare_rebind.png || true
	rm -f *.ppm

	# Make fastforwarded trace
	${TRACER} -f 3-5 -o sample_$1_ff.gfxr $REPLAYER -m none $TRACEDIR/sample_$1.gfxr
	mv sample_$1_ff* $TRACEDIR/sample_$1_ff_frame3.gfxr

	# Run the fastforwarded trace
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=1 $REPLAYER -m none $TRACEDIR/sample_$1_ff_frame3.gfxr
	convert -alpha off 1.ppm $REPORTDIR/sample_$1_f3_ff_replay.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_ff_replay.png $REPORTDIR/sample_$1_f3_compare_ff.png || true

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_native.png" /><br></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay.png" /><br><img $HTMLIMGOPTS src="sample_$1_f3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_remap.png" /><br><img $HTMLIMGOPTS src="sample_$1_f3_compare_remap.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_realign.png" /><br><img $HTMLIMGOPTS src="sample_$1_f3_compare_realign.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_rebind.png" /><br><img $HTMLIMGOPTS src="sample_$1_f3_compare_rebind.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_ff_replay.png" /><br><img $HTMLIMGOPTS src="sample_$1_f3_compare_ff.png" /></td>" >> $REPORT
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
run buffer_device_address # '-m rebind' may not support the replay of captured device addresses
run conservative_rasterization
run debug_utils
run descriptor_indexing
run dynamic_rendering
run fragment_shading_rate
run fragment_shading_rate_dynamic
#run open_gl_interop # hangs
#run portability
run push_descriptors
run ray_queries
run ray_tracing_reflection
run raytracing_basic
run raytracing_extended
run synchronization_2
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
run timestamp_queries
run calibrated_timestamps
run conditional_rendering
run descriptor_buffer_basic
run extended_dynamic_state2
run hlsl_shaders
run fragment_shader_barycentric
run graphics_pipeline_library
run logic_op_dynamic_state
run memory_budget
#run mesh_shader_culling
#run mesh_shading
run vertex_dynamic_state

echo "</table></body></html>" >> $REPORT
