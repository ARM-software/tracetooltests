unset VK_INSTANCE_LAYERS
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

demo triangle
demo bloom
demo computecloth
demo computecullandlod
exit
#demo computeheadless # not able to run non-interactive
demo computenbody
demo computeparticles
demo computeraytracing
demo computeshader
( vulkaninfo | grep -e VK_EXT_conditional_rendering > /dev/null ) && demo conditionalrender
( vulkaninfo | grep -e VK_EXT_conservative_rasterization > /dev/null ) && demo conservativeraster
demo debugutils
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
( vulkaninfo | grep -e VK_EXT_mesh_shader > /dev/null ) && demo meshshader
demo multisampling
demo multithreading
( vulkaninfo | grep -e VK_KHR_multiview > /dev/null ) && demo multiview
demo negativeviewportheight
demo occlusionquery
demo offscreen
demo oit
demo parallaxmapping
demo pbrbasic
demo pbribl
demo pbrtexture
demo pipelines
demo pipelinestatistics
demo pushconstants
demo pushdescriptors
demo radialblur
( vulkaninfo | grep -e VK_KHR_shader_non_semantic_info > /dev/null ) && demo debugprintf
( vulkaninfo | grep -e VK_KHR_ray_query > /dev/null ) && demo rayquery
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingbasic
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingcallable
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingreflections
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingshadows
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingsbtdata
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingintersection
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracingtextures
( vulkaninfo | grep -e VK_KHR_ray_tracing_pipeline > /dev/null ) && demo raytracinggltf
( vulkaninfo | grep -e VK_KHR_ray_tracing_position_fetch > /dev/null ) && demo raytracingpositionfetch
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
