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
run conservative_rasterization
run debug_utils
run descriptor_indexing
run dynamic_rendering
run fragment_shading_rate
run fragment_shading_rate_dynamic
#run open_gl_interop # hangs
run portability
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
run mesh_shader_culling
run mesh_shading
run vertex_dynamic_state
