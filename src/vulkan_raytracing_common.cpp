#include "vulkan_raytracing_common.h"

namespace ray_tracing_common
{
struct Vertex
{
	float pos[3];
};

static const std::vector<Vertex> kTriangleVertices = {
	{{1.0f, 1.0f, 0.0f}},
	{{-1.0f, 1.0f, 0.0f}},
	{{0.0f, -1.0f, 0.0f}},
};

static const std::vector<uint32_t> kTriangleIndices = {0, 1, 2};

void init_context(const vulkan_setup_t& vulkan, Context& context)
{
	context.functions = acceleration_structures::query_acceleration_structure_functions(vulkan);

	VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	pool_info.queueFamilyIndex = 0;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	check(vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &context.command_pool));

	VkCommandBufferAllocateInfo alloc_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	alloc_info.commandPool = context.command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	check(vkAllocateCommandBuffers(vulkan.device, &alloc_info, &context.command_buffer));

	vkGetDeviceQueue(vulkan.device, 0, 0, &context.queue);
}

void destroy_context(const vulkan_setup_t& vulkan, Context& context)
{
	vkDestroyCommandPool(vulkan.device, context.command_pool, nullptr);
	context.command_pool = VK_NULL_HANDLE;
	context.command_buffer = VK_NULL_HANDLE;
	context.queue = VK_NULL_HANDLE;
}

void build_simple_triangle_as(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel)
{
	accel.vertex_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		kTriangleVertices.size() * sizeof(Vertex),
		const_cast<Vertex*>(kTriangleVertices.data()),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	accel.vertex_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, accel.vertex_buffer.handle);

	accel.index_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		kTriangleIndices.size() * sizeof(uint32_t),
		const_cast<uint32_t*>(kTriangleIndices.data()),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	accel.index_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, accel.index_buffer.handle);

	const uint32_t primitive_count = 1;

	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry.geometry.triangles.vertexData = accel.vertex_buffer.address;
	geometry.geometry.triangles.maxVertex = 3;
	geometry.geometry.triangles.vertexStride = sizeof(Vertex);
	geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geometry.geometry.triangles.indexData = accel.index_buffer.address;

	VkAccelerationStructureBuildGeometryInfoKHR build_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build_info.geometryCount = 1;
	build_info.pGeometries = &geometry;

	VkAccelerationStructureBuildSizesInfoKHR build_sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	context.functions.vkGetAccelerationStructureBuildSizesKHR(
		vulkan.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&build_info,
		&primitive_count,
		&build_sizes);

	accel.blas_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		build_sizes.accelerationStructureSize,
		nullptr,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
	create_info.buffer = accel.blas_buffer.handle;
	create_info.size = build_sizes.accelerationStructureSize;
	create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	check(context.functions.vkCreateAccelerationStructureKHR(vulkan.device, &create_info, nullptr, &accel.blas.handle));

	acceleration_structures::Buffer scratch = acceleration_structures::prepare_buffer(
		vulkan,
		build_sizes.buildScratchSize,
		nullptr,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	scratch.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, scratch.handle);

	build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info.dstAccelerationStructure = accel.blas.handle;
	build_info.scratchData.deviceAddress = scratch.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR range_info{};
	range_info.primitiveCount = primitive_count;
	range_info.primitiveOffset = 0;
	range_info.firstVertex = 0;
	range_info.transformOffset = 0;
	VkAccelerationStructureBuildRangeInfoKHR* range_infos = &range_info;

	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkBeginCommandBuffer(context.command_buffer, &begin_info));
	context.functions.vkCmdBuildAccelerationStructuresKHR(context.command_buffer, 1, &build_info, &range_infos);
	check(vkEndCommandBuffer(context.command_buffer));

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &context.command_buffer;
	check(vkQueueSubmit(context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(context.queue));

	VkAccelerationStructureDeviceAddressInfoKHR blas_addr_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr};
	blas_addr_info.accelerationStructure = accel.blas.handle;
	accel.blas.address.deviceAddress = context.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &blas_addr_info);

	vkFreeMemory(vulkan.device, scratch.memory, nullptr);
	vkDestroyBuffer(vulkan.device, scratch.handle, nullptr);

	static VkTransformMatrixKHR identity = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
	};

	VkAccelerationStructureInstanceKHR instance{};
	instance.transform = identity;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = 0;
	instance.accelerationStructureReference = accel.blas.address.deviceAddress;

	accel.instance_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(VkAccelerationStructureInstanceKHR),
		&instance,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	accel.instance_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, accel.instance_buffer.handle);

	VkAccelerationStructureGeometryInstancesDataKHR instance_data{};
	instance_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instance_data.arrayOfPointers = VK_FALSE;
	instance_data.data = accel.instance_buffer.address;

	VkAccelerationStructureBuildRangeInfoKHR tlas_range{};
	tlas_range.primitiveCount = 1;
	tlas_range.primitiveOffset = 0;
	tlas_range.firstVertex = 0;
	tlas_range.transformOffset = 0;

	VkAccelerationStructureGeometryKHR tlas_geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	tlas_geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	tlas_geom.geometry.instances = instance_data;

	VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	tlas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	tlas_build_info.geometryCount = 1;
	tlas_build_info.pGeometries = &tlas_geom;
	tlas_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	tlas_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

	VkAccelerationStructureBuildSizesInfoKHR tlas_sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	context.functions.vkGetAccelerationStructureBuildSizesKHR(
		vulkan.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_OR_DEVICE_KHR,
		&tlas_build_info,
		&tlas_range.primitiveCount,
		&tlas_sizes);

	accel.tlas_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		tlas_sizes.accelerationStructureSize,
		nullptr,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR tlas_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
	tlas_create_info.type = tlas_build_info.type;
	tlas_create_info.size = tlas_sizes.accelerationStructureSize;
	tlas_create_info.buffer = accel.tlas_buffer.handle;
	tlas_create_info.offset = 0;
	check(context.functions.vkCreateAccelerationStructureKHR(vulkan.device, &tlas_create_info, nullptr, &accel.tlas.handle));

	tlas_build_info.dstAccelerationStructure = accel.tlas.handle;

	acceleration_structures::Buffer tlas_scratch = acceleration_structures::prepare_buffer(
		vulkan,
		tlas_sizes.buildScratchSize,
		nullptr,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	tlas_scratch.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, tlas_scratch.handle);
	tlas_build_info.scratchData.deviceAddress = tlas_scratch.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR* tlas_ranges = &tlas_range;
	check(vkResetCommandBuffer(context.command_buffer, 0));
	check(vkBeginCommandBuffer(context.command_buffer, &begin_info));
	context.functions.vkCmdBuildAccelerationStructuresKHR(context.command_buffer, 1, &tlas_build_info, &tlas_ranges);
	check(vkEndCommandBuffer(context.command_buffer));
	check(vkQueueSubmit(context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(context.queue));

	vkFreeMemory(vulkan.device, tlas_scratch.memory, nullptr);
	vkDestroyBuffer(vulkan.device, tlas_scratch.handle, nullptr);
}

void destroy_simple_triangle_as(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel)
{
	context.functions.vkDestroyAccelerationStructureKHR(vulkan.device, accel.blas.handle, nullptr);
	context.functions.vkDestroyAccelerationStructureKHR(vulkan.device, accel.tlas.handle, nullptr);

	vkFreeMemory(vulkan.device, accel.blas_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, accel.blas_buffer.handle, nullptr);

	vkFreeMemory(vulkan.device, accel.tlas_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, accel.tlas_buffer.handle, nullptr);

	vkFreeMemory(vulkan.device, accel.instance_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, accel.instance_buffer.handle, nullptr);

	vkFreeMemory(vulkan.device, accel.index_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, accel.index_buffer.handle, nullptr);

	vkFreeMemory(vulkan.device, accel.vertex_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, accel.vertex_buffer.handle, nullptr);
}
} // namespace ray_tracing_common
