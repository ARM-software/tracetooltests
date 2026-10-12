unset VK_INSTANCE_LAYERS
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

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
( vulkaninfo | grep -e VK_EXT_conservative_rasterization > /dev/null ) && run conservative_rasterization
run debug_utils
run descriptor_indexing
run dynamic_rendering
( vulkaninfo | grep -e VK_KHR_fragment_shading_rate > /dev/null ) && run fragment_shading_rate
( vulkaninfo | grep -e VK_KHR_fragment_shading_rate > /dev/null ) && run fragment_shading_rate_dynamic
run portability
run push_descriptors
run ray_queries
run ray_tracing_reflection
run synchronization_2
run timeline_semaphore
run 16bit_arithmetic
( vulkaninfo | grep -e storageInputOutput16 > /dev/null ) && run 16bit_storage_input_output
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
run fragment_shader_barycentric
( vulkaninfo | grep -e VK_KHR_pipeline_library > /dev/null ) && run graphics_pipeline_library
run logic_op_dynamic_state
run memory_budget
run mesh_shader_culling
run mesh_shading
( vulkaninfo | grep -e VK_EXT_vertex_input_dynamic_state > /dev/null ) && run vertex_dynamic_state
run hello_triangle_1_3
run oit_depth_peeling
run oit_linked_lists
run swapchain_recreation
run color_write_enable
run compute_shader_derivatives
run dynamic_blending
run dynamic_line_rasterization
run dynamic_multisample_rasterization
run dynamic_primitive_clipping
run dynamic_rendering_local_read
( vulkaninfo | grep -e VK_EXT_fragment_density_map > /dev/null ) && run fragment_density_map
run gshader_to_mshader
run host_image_copy
run patch_control_points
run rasterization_order_attachment_access
#run ray_tracing_invocation_reorder -- uses Vulkan EXT but nvidia only spirv
run ray_tracing_position_fetch
run shader_debugprintf
#run shader_object -- requires VK_LAYER_KHRONOS_shader_object
run shader_relaxed_extended_instruction
run sparse_image
run image_compression_control
run calibrated_timestamps
run color_write_enable
run compute_shader_derivatives
run descriptor_buffer_basic
run dynamic_blending
run dynamic_line_rasterization
run dynamic_multisample_rasterization
run dynamic_primitive_clipping
run dynamic_rendering_local_read
run extended_dynamic_state2
run fragment_density_map
run fragment_shader_barycentric
run full_screen_exclusive
run gshader_to_mshader
run host_image_copy
run logic_op_dynamic_state
run memory_budget
run mesh_shader_culling
run mesh_shading
run patch_control_points
un rasterization_order_attachment_access
run ray_tracing_basic
run ray_tracing_extended
run ray_tracing_invocation_reorder
run ray_tracing_position_fetch
run shader_debugprintf
run shader_object
run shader_relaxed_extended_instruction
#run sparse_image
run descriptor_heap
run device_fault
run mobile_nerf
run mobile_nerf_rayquery
run pipeline_binary
run render_octomap
run shader_quad_control
run swapchain_present_timing
#run layer_settings

# -- Tensor stuff (add emulation layers)
#runtensor tensor_and_data_graph
#runtensor compute_shaders_with_tensors
#runtensor simple_tensor_and_data_graph
#runtensor tensor_image_aliasing
#runtensor postprocessing_with_vgf

# -- API interop stuff
#run open_gl_interop # hangs
#run open_cl_common
#run open_cl_interop
#run open_cl_interop_arm

# -- HPP nonsense
#run hpp_compute_nbody
#run hpp_dynamic_uniform_buffers
#run hpp_hdr
#run hpp_hello_triangle
#run hpp_hello_triangle_1_3
#run hpp_instancing
#run hpp_mesh_shading
#run hpp_oit_depth_peeling
#run hpp_oit_linked_lists
#run hpp_pipeline_cache
#run hpp_push_descriptors
#run hpp_separate_image_sampler
#run hpp_swapchain_images
#run hpp_terrain_tessellation
#run hpp_texture_compression_comparison
#run hpp_texture_loading
#run hpp_texture_mipmap_generation
#run hpp_timestamp_queries

# -- Not actual tests (possibly disabled)
#run graph_constants
