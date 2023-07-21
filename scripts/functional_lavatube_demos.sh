#!/bin/bash

REPORTDIR=reports/lavatube/demos${TAG}
REPORT=$REPORTDIR/report.html
DEMO_PARAMS="--benchmark -bfs 10"
TRACEDIR=traces${TAG}

rm -f external/vulkan-demos/*.ppm *.ppm $TRACEDIR/demo_*.vk $REPORTDIR/*.png $REPORTDIR/*.html
mkdir -p $TRACEDIR $REPORTDIR

unset VK_INSTANCE_LAYERS
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

LAVATUBE_PATH=/work/lava/build
HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-demos with lavatube</h1><table><tr><th>Name</th><th>Original</th><th>Replay original swapchain</th><th>Replay virtual swapchain</th></tr>" >> $REPORT

function demo
{
	echo
	echo "****** $1 ******"
	echo

	echo
	echo "** native $1 **"
	echo

	rm -f external/vulkan-demos/build/bin/*.ppm *.ppm external/vulkan-demos/*.ppm

	# Native run
	rm -f external/vulkan-demos/build/bin/*.ppm
	( cd external/vulkan-demos/build/bin ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 ./$1 $DEMO_PARAMS )
	convert -alpha off external/vulkan-demos/build/bin/3.ppm $REPORTDIR/$1_f3_native.png
	rm -f external/vulkan-demos/build/bin/*.ppm

	echo
	echo "** trace $1 **"
	echo

	# Make trace
	export LAVATUBE_DESTINATION=demo_$1
	export VK_LAYER_PATH=$LAVATUBE_PATH/implicit_layer.d
	export LD_LIBRARY_PATH=$LAVATUBE_PATH/implicit_layer.d
	export VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube
	( cd external/vulkan-demos/build/bin ; ./$1 $DEMO_PARAMS )
	mv external/vulkan-demos/build/bin/demo_$1.vk $TRACEDIR/

	echo
	echo "** replay $1 **"
	echo

	# Replay
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $LAVATUBE_PATH/replay $TRACEDIR/demo_$1.vk
	convert -alpha off 3.ppm $REPORTDIR/$1_f3_replay.png
	rm -f *.ppm
	compare -alpha off $REPORTDIR/$1_f3_native.png $REPORTDIR/$1_f3_replay.png $REPORTDIR/$1_f3_compare.png || true

	echo
	echo "** replay $1 virtual **"
	echo

	# Replay
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $LAVATUBE_PATH/replay -v $TRACEDIR/demo_$1.vk
	convert -alpha off 3.ppm $REPORTDIR/$1_f3_replay_virtual.png
	rm -f *.ppm
	compare -alpha off $REPORTDIR/$1_f3_native.png $REPORTDIR/$1_f3_replay_virtual.png $REPORTDIR/$1_f3_compare_virtual.png || true

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_replay.png" /><img $HTMLIMGOPTS src="$1_f3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_replay_virtual.png" /><img $HTMLIMGOPTS src="$1_f3_compare_virtual.png" /></td></tr>" >> $REPORT
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
#demo shadowmappingcascade # out of memory on replay without -H 0
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
