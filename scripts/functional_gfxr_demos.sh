#!/bin/bash

REPORTDIR=reports/gfxr/demos${TAG}
REPORT=$REPORTDIR/report.html
DEMO_PARAMS="--benchmark -bfs 10"
TRACEDIR=traces${TAG}
REPLAYER=${REPLAYER:-"$(which gfxrecon-replay)"}

rm -rf external/vulkan-demos/*.ppm *.ppm $TRACEDIR/demo_*.gfxr $REPORTDIR external/vulkan-demos/*.gfxr
mkdir -p $TRACEDIR
mkdir -p $REPORTDIR

unset VK_INSTANCE_LAYERS
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-demos with gfxreconstruct</h1><table><tr><th>Name</th><th>Original</th><th>Replay</th><th>Replay -m remap</th><th>Replay -m realign</th><th>Replay -m rebind</th></tr>" >> $REPORT

function demo
{
	echo
	echo "****** $1 ******"
	echo

	echo
	echo "** native **"
	echo

	rm -f external/vulkan-demos/*.ppm *.ppm

	# Native run
	( cd external/vulkan-demos ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 build/bin/$1 $DEMO_PARAMS )
	convert -alpha off external/vulkan-demos/3.ppm $REPORTDIR/$1_f3_native.png
	rm -f external/vulkan-demos/*.ppm *.ppm

	echo
	echo "** trace **"
	echo

	# Make trace
	( cd external/vulkan-demos ; gfxrecon-capture-vulkan.py -o demo_$1.gfxr build/bin/$1 $DEMO_PARAMS )
	mv external/vulkan-demos/demo_$1*.gfxr $TRACEDIR/demo_$1.gfxr

	echo
	echo "** replay using $REPLAYER **"
	echo

	# Replay
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER $TRACEDIR/demo_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/$1_f3_replay.png
	compare -alpha off $REPORTDIR/$1_f3_native.png $REPORTDIR/$1_f3_replay.png $REPORTDIR/$1_f3_compare.png || true
	rm -f *.ppm

	# Replay -m remap
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m remap $TRACEDIR/demo_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/$1_f3_replay_remap.png
	compare -alpha off $REPORTDIR/$1_f3_native.png $REPORTDIR/$1_f3_replay_remap.png $REPORTDIR/$1_f3_compare_remap.png || true
	rm -f *.ppm

	# Replay -m realign
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m realign $TRACEDIR/demo_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/$1_f3_replay_realign.png
	compare -alpha off $REPORTDIR/$1_f3_native.png $REPORTDIR/$1_f3_replay_realign.png $REPORTDIR/$1_f3_compare_realign.png || true
	rm -f *.ppm

	# Replay -m rebind
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m rebind $TRACEDIR/demo_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/$1_f3_replay_rebind.png
	compare -alpha off $REPORTDIR/$1_f3_native.png $REPORTDIR/$1_f3_replay_rebind.png $REPORTDIR/$1_f3_compare_rebind.png || true
	rm -f *.ppm

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_replay.png" /><img $HTMLIMGOPTS src="$1_f3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_replay_remap.png" /><img $HTMLIMGOPTS src="$1_f3_compare_remap.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_replay_realign.png" /><img $HTMLIMGOPTS src="$1_f3_compare_realign.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_replay_rebind.png" /><img $HTMLIMGOPTS src="$1_f3_compare_rebind.png" /></td></tr>" >> $REPORT
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
