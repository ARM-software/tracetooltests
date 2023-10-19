#!/bin/bash

REPORTDIR=reports/gfxr/demos${TAG}
REPORT=$REPORTDIR/report.html
DEMO_PARAMS="--benchmark -bfs 10"
TRACEDIR=traces${TAG}
REPLAYER=${REPLAYER:-"$(which gfxrecon-replay)"}

rm -rf external/vulkan-demos/*.ppm *.ppm $TRACEDIR/demo_*.gfxr $REPORTDIR external/vulkan-demos/*.gfxr
mkdir -p $TRACEDIR $REPORTDIR

unset VK_INSTANCE_LAYERS
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-demos with gfxreconstruct</h1><table><tr><th>Name</th><th>Original</th><th>Replay -m none</th>" >> $REPORT
echo "<th>Replay -m remap</th><th>Replay -m realign</th><th>Replay -m rebind</th><th>Replay offscreen</th><th>Replay trim</th></tr>" >> $REPORT

function demo_runner
{
	NAME="demo_$1_$3"

	echo
	echo "** native $1 **"
	echo

	rm -f external/vulkan-demos/*.ppm external/vulkan-demos/*.gfxr *.ppm

	# Native run
	( cd external/vulkan-demos ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 build/bin/$1 $DEMO_PARAMS )
	convert -alpha off external/vulkan-demos/3.ppm $REPORTDIR/${NAME}_f3_native.png
	rm -f external/vulkan-demos/*.ppm *.ppm

	echo
	echo "** trace $1 **"
	echo

	# Make trace
	( cd external/vulkan-demos ; gfxrecon-capture-vulkan.py -o ${NAME}.gfxr build/bin/$1 $DEMO_PARAMS )
	mv external/vulkan-demos/${NAME}*.gfxr $TRACEDIR/${NAME}.gfxr

	echo
	echo "** replay $1 using $REPLAYER **"
	echo

	# Replay -m none
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m none $TRACEDIR/${NAME}.gfxr
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay.png $REPORTDIR/${NAME}_f3_compare.png || true
	rm -f *.ppm

	# Replay -m remap
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m remap $TRACEDIR/${NAME}.gfxr
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay_remap.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_remap.png $REPORTDIR/${NAME}_f3_compare_remap.png || true
	rm -f *.ppm

	# Replay -m realign
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m realign $TRACEDIR/${NAME}.gfxr
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay_realign.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_realign.png $REPORTDIR/${NAME}_f3_compare_realign.png || true
	rm -f *.ppm

	# Replay -m rebind
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m rebind $TRACEDIR/${NAME}.gfxr
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay_rebind.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_rebind.png $REPORTDIR/${NAME}_f3_compare_rebind.png || true
	rm -f *.ppm

	# Replay --swapchain offscreen
	$REPLAYER --swapchain offscreen --screenshots 4 --screenshot-format png -m none $TRACEDIR/${NAME}.gfxr
	convert -alpha off screenshot_frame_4.png $REPORTDIR/${NAME}_f3_replay_offscreen.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_offscreen.png $REPORTDIR/${NAME}_f3_compare_offscreen.png || true
	rm -f *.png

	# Make fastforwarded traces
	gfxrecon-capture-vulkan.py -f 3-5 -o ${NAME}_ff.gfxr $REPLAYER -m none $TRACEDIR/$NAME.gfxr
	mv ${NAME}_ff* $TRACEDIR/${NAME}_ff_frame3.gfxr

	# Run the fastforwarded trace
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=1 $REPLAYER -m none $TRACEDIR/${NAME}_ff_frame3.gfxr
	convert -alpha off 1.ppm $REPORTDIR/${NAME}_f3_ff_replay.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_ff_replay.png $REPORTDIR/${NAME}_f3_compare_ff.png || true

	echo "<tr><td>$1 $3</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_remap.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_remap.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_realign.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_realign.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_rebind.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_rebind.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_offscreen.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_offscreen.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_ff_replay.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_ff.png" /></td>" >> $REPORT
	echo "</tr>" >> $REPORT
}

function demo
{
	echo
	echo "****** $1 ******"
	echo

	demo_runner $1 "" "default"
}

demo triangle
demo bloom
demo computecloth
demo computecullandlod
#demo computeheadless # not able to run non-interactive
demo computenbody
demo computeparticles
demo computeraytracing
demo computeshader
( vulkaninfo | grep -e VK_EXT_conditional_rendering > /dev/null ) && demo conditionalrender
( vulkaninfo | grep -e VK_EXT_conservative_rasterization > /dev/null ) && demo conservativeraster
demo debugmarker
demo deferred
demo deferredmultisampling
demo deferredshadows
( vulkaninfo | grep -e VK_EXT_descriptor_indexing > /dev/null ) && demo descriptorindexing
demo descriptorsets
demo displacement
demo distancefieldfonts
demo dynamicrendering
demo dynamicuniformbuffer
demo gears
demo geometryshader
demo gltfloading
demo gltfscenerendering
demo gltfskinning
( vulkaninfo | grep -e VK_EXT_graphics_pipeline_library > /dev/null ) && demo graphicspipelinelibrary
demo hdr
demo imgui
demo indirectdraw
( vulkaninfo | grep -e VK_EXT_inline_uniform_block > /dev/null ) && demo inlineuniformblocks
demo inputattachments
demo instancing
( vulkaninfo | grep -e VK_EXT_mesh_shader > /dev/null ) && demo mesh
demo multisampling
demo multithreading
( vulkaninfo | grep -e VK_KHR_multiview > /dev/null ) && demo multiview
demo negativeviewportheight
demo occlusionquery
demo offscreen
demo oit
demo parallaxmapping
demo particlefire
demo pbrbasic
demo pbribl
demo pbrtexture
demo pipelines
demo pipelinestatistics
demo pushconstants
demo pushdescriptors
demo radialblur
( vulkaninfo | grep -e VK_KHR_ray_query > /dev/null ) && demo rayquery
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingbasic
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingcallable
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingreflections
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingshadows
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingsbtdata
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingintersection
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingtextures
#demo renderheadless # not non-interactive
demo screenshot
demo shadowmapping
demo shadowmappingcascade
demo shadowmappingomni
demo specializationconstants
demo sphericalenvmapping
demo ssao
demo stencilbuffer
demo subpasses
demo terraintessellation
demo tessellation
demo textoverlay
demo texture
demo texture3d
demo texturearray
demo texturecubemap
demo texturecubemaparray
demo texturemipmapgen
( vulkaninfo | grep -e sparseResidencyImage2D | grep -e 1 > /dev/null ) && demo texturesparseresidency
#demo variablerateshading # uses VK_NV_shading_rate_image, not VK_KHR_fragment_shading_rate
demo vertexattributes
demo viewportarray
demo vulkanscene
demo occlusionquery
( vulkaninfo | grep -e VK_EXT_descriptor_buffer > /dev/null ) && demo descriptorbuffer
( vulkaninfo | grep -e VK_EXT_dynamic_state > /dev/null ) && demo dynamicstate

echo "</table></body></html>" >> $REPORT
