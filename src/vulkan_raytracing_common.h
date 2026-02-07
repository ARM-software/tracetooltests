#pragma once

#include "vulkan_common.h"

namespace ray_tracing_common
{
	struct Context
	{
		acceleration_structures::functions functions;
		VkCommandPool command_pool{VK_NULL_HANDLE};
		VkCommandBuffer command_buffer{VK_NULL_HANDLE};
		VkQueue queue{VK_NULL_HANDLE};
	};

	struct SimpleAS
	{
		acceleration_structures::AccelerationStructure blas;
		acceleration_structures::AccelerationStructure tlas;
		acceleration_structures::Buffer blas_buffer;
		acceleration_structures::Buffer tlas_buffer;
		acceleration_structures::Buffer instance_buffer;
		acceleration_structures::Buffer vertex_buffer;
		acceleration_structures::Buffer index_buffer;
	};

	void init_context(const vulkan_setup_t& vulkan, Context& context);
	void destroy_context(const vulkan_setup_t& vulkan, Context& context);

	void build_simple_triangle_as(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel);
	void destroy_simple_triangle_as(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel);
}
