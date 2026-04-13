#include "vulkan_raytracing_common.h"

namespace ray_tracing_common
{
struct Vertex
{
	float pos[3];
};

static const VkAabbPositionsKHR kSimpleAabb = {
	-1.0f, -1.0f, 0.0f,
	1.0f, 1.0f, 1.0f,
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

static void destroy_buffer(const vulkan_setup_t& vulkan, acceleration_structures::Buffer& buffer)
{
	if (buffer.memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(vulkan.device, buffer.memory, nullptr);
		buffer.memory = VK_NULL_HANDLE;
	}

	if (buffer.handle != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan.device, buffer.handle, nullptr);
		buffer.handle = VK_NULL_HANDLE;
	}

	buffer.address.deviceAddress = 0;
	buffer.address.hostAddress = nullptr;
}

static void build_blas(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel,
                       const VkAccelerationStructureGeometryKHR& geometry, uint32_t primitive_count)
{
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
	check(vkResetCommandBuffer(context.command_buffer, 0));
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
}

static void build_single_instance_tlas(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel)
{
	static VkTransformMatrixKHR identity = {{
		{1.0f, 0.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 1.0f, 0.0f},
	}};

	VkAccelerationStructureInstanceKHR instance{};
	instance.transform = identity;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = 0;
	instance.accelerationStructureReference = accel.blas.address.deviceAddress;
	VkMarkingTypeARM marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
	VkMarkingSubTypeARM sub_type{};
	sub_type.deviceAddressType = VK_DEVICE_ADDRESS_TYPE_ACCELERATION_STRUCTURE_ARM;
	VkDeviceSize marked_offset = offsetof(VkAccelerationStructureInstanceKHR, accelerationStructureReference);
	VkMarkedOffsetsARM markings{VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr};
	markings.count = 1;
	markings.pMarkingTypes = &marking_type;
	markings.pSubTypes = &sub_type;
	markings.pOffsets = &marked_offset;

	accel.instance_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(VkAccelerationStructureInstanceKHR),
		&instance,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vulkan.has_trace_helpers ? &markings : nullptr);
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
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &context.command_buffer;
	check(vkResetCommandBuffer(context.command_buffer, 0));
	check(vkBeginCommandBuffer(context.command_buffer, &begin_info));
	context.functions.vkCmdBuildAccelerationStructuresKHR(context.command_buffer, 1, &tlas_build_info, &tlas_ranges);
	check(vkEndCommandBuffer(context.command_buffer));
	check(vkQueueSubmit(context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(context.queue));

	vkFreeMemory(vulkan.device, tlas_scratch.memory, nullptr);
	vkDestroyBuffer(vulkan.device, tlas_scratch.handle, nullptr);
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

	build_blas(vulkan, context, accel, geometry, 1);
	build_single_instance_tlas(vulkan, context, accel);
}

void build_simple_aabb_as(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel)
{
	accel.geometry_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(kSimpleAabb),
		const_cast<VkAabbPositionsKHR*>(&kSimpleAabb),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	accel.geometry_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, accel.geometry_buffer.handle);

	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
	geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
	geometry.geometry.aabbs.data = accel.geometry_buffer.address;
	geometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

	build_blas(vulkan, context, accel, geometry, 1);
	build_single_instance_tlas(vulkan, context, accel);
}

void destroy_simple_triangle_as(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel)
{
	if (accel.blas.handle != VK_NULL_HANDLE)
	{
		context.functions.vkDestroyAccelerationStructureKHR(vulkan.device, accel.blas.handle, nullptr);
		accel.blas.handle = VK_NULL_HANDLE;
		accel.blas.address.deviceAddress = 0;
	}

	if (accel.tlas.handle != VK_NULL_HANDLE)
	{
		context.functions.vkDestroyAccelerationStructureKHR(vulkan.device, accel.tlas.handle, nullptr);
		accel.tlas.handle = VK_NULL_HANDLE;
		accel.tlas.address.deviceAddress = 0;
	}

	destroy_buffer(vulkan, accel.blas_buffer);
	destroy_buffer(vulkan, accel.tlas_buffer);
	destroy_buffer(vulkan, accel.instance_buffer);
	destroy_buffer(vulkan, accel.geometry_buffer);
	destroy_buffer(vulkan, accel.index_buffer);
	destroy_buffer(vulkan, accel.vertex_buffer);
}

void destroy_simple_aabb_as(const vulkan_setup_t& vulkan, Context& context, SimpleAS& accel)
{
	destroy_simple_triangle_as(vulkan, context, accel);
}

std::vector<uint8_t> readback_storage_image(const vulkan_setup_t& vulkan, Context& context,
	VkImage image, VkImageLayout layout, VkFormat format, uint32_t width, uint32_t height, const char* filename)
{
	assert(format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
	       format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB);

	const VkDeviceSize byte_size = static_cast<VkDeviceSize>(width) * height * 4;
	acceleration_structures::Buffer readback = acceleration_structures::prepare_buffer(
		vulkan,
		byte_size,
		nullptr,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(context.command_buffer, 0));
	check(vkBeginCommandBuffer(context.command_buffer, &begin_info));

	VkImageMemoryBarrier image_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr};
	image_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	image_barrier.oldLayout = layout;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	image_barrier.image = image;
	image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.subresourceRange.baseMipLevel = 0;
	image_barrier.subresourceRange.levelCount = 1;
	image_barrier.subresourceRange.baseArrayLayer = 0;
	image_barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(context.command_buffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&image_barrier);

	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = {width, height, 1};

	vkCmdCopyImageToBuffer(context.command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &region);

	VkBufferMemoryBarrier buffer_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
	buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	buffer_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	buffer_barrier.buffer = readback.handle;
	buffer_barrier.offset = 0;
	buffer_barrier.size = byte_size;

	vkCmdPipelineBarrier(context.command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_HOST_BIT,
		0,
		0,
		nullptr,
		1,
		&buffer_barrier,
		0,
		nullptr);

	check(vkEndCommandBuffer(context.command_buffer));

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &context.command_buffer;
	check(vkQueueSubmit(context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(context.queue));

	if (filename != nullptr)
	{
		test_save_image(vulkan, filename, readback.memory, 0, width, height, format);
	}

	void* mapped = nullptr;
	check(vkMapMemory(vulkan.device, readback.memory, 0, byte_size, 0, &mapped));
	std::vector<uint8_t> bytes(byte_size);
	memcpy(bytes.data(), mapped, byte_size);
	vkUnmapMemory(vulkan.device, readback.memory);

	destroy_buffer(vulkan, readback);
	return bytes;
}
} // namespace ray_tracing_common
