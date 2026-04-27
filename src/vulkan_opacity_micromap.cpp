#include "vulkan_common.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

using Buffer = acceleration_structures::Buffer;
using AccelerationStructure = acceleration_structures::AccelerationStructure;

namespace
{
struct MicromapFunctions
{
	PFN_vkCreateMicromapEXT vkCreateMicromapEXT = nullptr;
	PFN_vkDestroyMicromapEXT vkDestroyMicromapEXT = nullptr;
	PFN_vkCmdBuildMicromapsEXT vkCmdBuildMicromapsEXT = nullptr;
	PFN_vkCmdCopyMicromapEXT vkCmdCopyMicromapEXT = nullptr;
	PFN_vkCmdCopyMicromapToMemoryEXT vkCmdCopyMicromapToMemoryEXT = nullptr;
	PFN_vkCmdCopyMemoryToMicromapEXT vkCmdCopyMemoryToMicromapEXT = nullptr;
	PFN_vkCmdWriteMicromapsPropertiesEXT vkCmdWriteMicromapsPropertiesEXT = nullptr;
	PFN_vkGetDeviceMicromapCompatibilityEXT vkGetDeviceMicromapCompatibilityEXT = nullptr;
	PFN_vkGetMicromapBuildSizesEXT vkGetMicromapBuildSizesEXT = nullptr;
};

struct Vertex
{
	float pos[3];
};

struct AlignedBufferAddress
{
	VkDeviceAddress device_address = 0;
	VkDeviceSize offset = 0;
};

struct Resources
{
	acceleration_structures::functions accel;
	MicromapFunctions micromap;

	VkQueue queue = VK_NULL_HANDLE;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkQueryPool compacted_query_pool = VK_NULL_HANDLE;
	VkQueryPool serialization_query_pool = VK_NULL_HANDLE;

	Buffer data_buffer;
	Buffer triangle_buffer;
	Buffer vertex_buffer;
	Buffer index_buffer;
	Buffer source_storage_buffer;
	Buffer compacted_storage_buffer;
	Buffer serialized_buffer;
	Buffer deserialized_storage_buffer;
	Buffer blas_buffer;

	VkMicromapEXT source_micromap = VK_NULL_HANDLE;
	VkMicromapEXT compacted_micromap = VK_NULL_HANDLE;
	VkMicromapEXT deserialized_micromap = VK_NULL_HANDLE;
	AccelerationStructure blas;

	AlignedBufferAddress data_address;
	AlignedBufferAddress triangle_address;
	AlignedBufferAddress serialized_address;
};

static const std::array<Vertex, 3> kVertices = {{
	{{1.0f, 1.0f, 0.0f}},
	{{-1.0f, 1.0f, 0.0f}},
	{{0.0f, -1.0f, 0.0f}},
}};

static const std::array<uint32_t, 3> kIndices = {{0, 1, 2}};
static const std::array<uint8_t, 1> kMicromapData = {{0}};

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
	assert(alignment != 0);
	return (value + alignment - 1) & ~(alignment - 1);
}

static void map_and_write(const vulkan_setup_t& vulkan, const Buffer& buffer, VkDeviceSize offset, const void* data, VkDeviceSize size)
{
	assert(buffer.memory != VK_NULL_HANDLE);
	void* mapped = nullptr;
	check(vkMapMemory(vulkan.device, buffer.memory, 0, offset + size, 0, &mapped));
	memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);
	if (vulkan.has_explicit_host_updates) testFlushMemory(vulkan, buffer.memory, offset, size, true);
	vkUnmapMemory(vulkan.device, buffer.memory);
}

static AlignedBufferAddress get_aligned_device_address(const vulkan_setup_t& vulkan, const Buffer& buffer, VkDeviceSize alignment)
{
	AlignedBufferAddress aligned{};
	const VkDeviceAddress base = acceleration_structures::get_buffer_device_address(vulkan, buffer.handle);
	const VkDeviceAddress aligned_base = align_up(base, alignment);
	aligned.device_address = aligned_base;
	aligned.offset = aligned_base - base;
	return aligned;
}

static void destroy_buffer(const vulkan_setup_t& vulkan, Buffer& buffer)
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

static void destroy_micromap(const vulkan_setup_t& vulkan, const MicromapFunctions& functions, VkMicromapEXT& micromap)
{
	if (micromap != VK_NULL_HANDLE)
	{
		functions.vkDestroyMicromapEXT(vulkan.device, micromap, nullptr);
		micromap = VK_NULL_HANDLE;
	}
}

static void begin_commands(VkCommandBuffer command_buffer)
{
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(command_buffer, 0));
	check(vkBeginCommandBuffer(command_buffer, &begin_info));
}

static void submit_and_wait(VkQueue queue, VkCommandBuffer command_buffer)
{
	check(vkEndCommandBuffer(command_buffer));
	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	check(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(queue));
}

static MicromapFunctions query_micromap_functions(const vulkan_setup_t& vulkan)
{
	MicromapFunctions functions{};
	functions.vkCreateMicromapEXT = reinterpret_cast<PFN_vkCreateMicromapEXT>(vkGetDeviceProcAddr(vulkan.device, "vkCreateMicromapEXT"));
	functions.vkDestroyMicromapEXT = reinterpret_cast<PFN_vkDestroyMicromapEXT>(vkGetDeviceProcAddr(vulkan.device, "vkDestroyMicromapEXT"));
	functions.vkCmdBuildMicromapsEXT = reinterpret_cast<PFN_vkCmdBuildMicromapsEXT>(vkGetDeviceProcAddr(vulkan.device, "vkCmdBuildMicromapsEXT"));
	functions.vkCmdCopyMicromapEXT = reinterpret_cast<PFN_vkCmdCopyMicromapEXT>(vkGetDeviceProcAddr(vulkan.device, "vkCmdCopyMicromapEXT"));
	functions.vkCmdCopyMicromapToMemoryEXT = reinterpret_cast<PFN_vkCmdCopyMicromapToMemoryEXT>(vkGetDeviceProcAddr(vulkan.device, "vkCmdCopyMicromapToMemoryEXT"));
	functions.vkCmdCopyMemoryToMicromapEXT = reinterpret_cast<PFN_vkCmdCopyMemoryToMicromapEXT>(vkGetDeviceProcAddr(vulkan.device, "vkCmdCopyMemoryToMicromapEXT"));
	functions.vkCmdWriteMicromapsPropertiesEXT = reinterpret_cast<PFN_vkCmdWriteMicromapsPropertiesEXT>(vkGetDeviceProcAddr(vulkan.device, "vkCmdWriteMicromapsPropertiesEXT"));
	functions.vkGetDeviceMicromapCompatibilityEXT =
		reinterpret_cast<PFN_vkGetDeviceMicromapCompatibilityEXT>(vkGetDeviceProcAddr(vulkan.device, "vkGetDeviceMicromapCompatibilityEXT"));
	functions.vkGetMicromapBuildSizesEXT = reinterpret_cast<PFN_vkGetMicromapBuildSizesEXT>(vkGetDeviceProcAddr(vulkan.device, "vkGetMicromapBuildSizesEXT"));

	assert(functions.vkCreateMicromapEXT);
	assert(functions.vkDestroyMicromapEXT);
	assert(functions.vkCmdBuildMicromapsEXT);
	assert(functions.vkCmdCopyMicromapEXT);
	assert(functions.vkCmdCopyMicromapToMemoryEXT);
	assert(functions.vkCmdCopyMemoryToMicromapEXT);
	assert(functions.vkCmdWriteMicromapsPropertiesEXT);
	assert(functions.vkGetDeviceMicromapCompatibilityEXT);
	assert(functions.vkGetMicromapBuildSizesEXT);
	return functions;
}

static void init_resources(const vulkan_setup_t& vulkan, Resources& resources)
{
	resources.accel = acceleration_structures::query_acceleration_structure_functions(vulkan);
	resources.micromap = query_micromap_functions(vulkan);

	VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	pool_info.queueFamilyIndex = 0;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	check(vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &resources.command_pool));

	VkCommandBufferAllocateInfo alloc_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	alloc_info.commandPool = resources.command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	check(vkAllocateCommandBuffers(vulkan.device, &alloc_info, &resources.command_buffer));

	vkGetDeviceQueue(vulkan.device, 0, 0, &resources.queue);

	VkQueryPoolCreateInfo compacted_query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr};
	compacted_query_info.queryType = VK_QUERY_TYPE_MICROMAP_COMPACTED_SIZE_EXT;
	compacted_query_info.queryCount = 1;
	check(vkCreateQueryPool(vulkan.device, &compacted_query_info, nullptr, &resources.compacted_query_pool));

	VkQueryPoolCreateInfo serialization_query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr};
	serialization_query_info.queryType = VK_QUERY_TYPE_MICROMAP_SERIALIZATION_SIZE_EXT;
	serialization_query_info.queryCount = 1;
	check(vkCreateQueryPool(vulkan.device, &serialization_query_info, nullptr, &resources.serialization_query_pool));

	resources.vertex_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(kVertices),
		const_cast<Vertex*>(kVertices.data()),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	resources.vertex_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.vertex_buffer.handle);

	resources.index_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(kIndices),
		const_cast<uint32_t*>(kIndices.data()),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	resources.index_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.index_buffer.handle);

	resources.data_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		512,
		nullptr,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	resources.data_address = get_aligned_device_address(vulkan, resources.data_buffer, 256);
	map_and_write(vulkan, resources.data_buffer, resources.data_address.offset, kMicromapData.data(), kMicromapData.size());

	const VkMicromapTriangleEXT triangle = { 0, 0, VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT };
	resources.triangle_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		512,
		nullptr,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	resources.triangle_address = get_aligned_device_address(vulkan, resources.triangle_buffer, 256);
	map_and_write(vulkan, resources.triangle_buffer, resources.triangle_address.offset, &triangle, sizeof(triangle));

	assert((resources.data_address.device_address & 255) == 0);
	assert((resources.triangle_address.device_address & 255) == 0);
}

static VkMicromapEXT create_micromap(const vulkan_setup_t& vulkan, Resources& resources, Buffer& storage_buffer, VkDeviceSize size, const char* name)
{
	storage_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		size,
		nullptr,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkMicromapCreateInfoEXT create_info{VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT, nullptr};
	create_info.buffer = storage_buffer.handle;
	create_info.size = size;
	create_info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;

	VkMicromapEXT micromap = VK_NULL_HANDLE;
	check(resources.micromap.vkCreateMicromapEXT(vulkan.device, &create_info, nullptr, &micromap));
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)storage_buffer.handle, name);
	test_set_name(vulkan, VK_OBJECT_TYPE_MICROMAP_EXT, (uint64_t)micromap, name);
	return micromap;
}

static void build_source_micromap(const vulkan_setup_t& vulkan, Resources& resources, const VkMicromapUsageEXT& usage,
                                  VkMicromapBuildSizesInfoEXT& build_sizes)
{
	VkMicromapBuildInfoEXT build_info{VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT, nullptr};
	build_info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
	build_info.flags = VK_BUILD_MICROMAP_ALLOW_COMPACTION_BIT_EXT | VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
	build_info.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
	build_info.usageCountsCount = 1;
	build_info.pUsageCounts = &usage;
	build_info.data.deviceAddress = resources.data_address.device_address;
	build_info.triangleArray.deviceAddress = resources.triangle_address.device_address;
	build_info.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

	resources.micromap.vkGetMicromapBuildSizesEXT(vulkan.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &build_sizes);
	assert(build_sizes.micromapSize > 0);

	resources.source_micromap = create_micromap(vulkan, resources, resources.source_storage_buffer, build_sizes.micromapSize, "source_opacity_micromap");

	Buffer scratch_buffer{};
	if (build_sizes.buildScratchSize != 0)
	{
		scratch_buffer = acceleration_structures::prepare_buffer(
			vulkan,
			build_sizes.buildScratchSize,
			nullptr,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		scratch_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, scratch_buffer.handle);
	}

	build_info.dstMicromap = resources.source_micromap;
	build_info.scratchData.deviceAddress = scratch_buffer.address.deviceAddress;

	begin_commands(resources.command_buffer);
	vkCmdResetQueryPool(resources.command_buffer, resources.compacted_query_pool, 0, 1);
	vkCmdResetQueryPool(resources.command_buffer, resources.serialization_query_pool, 0, 1);
	resources.micromap.vkCmdBuildMicromapsEXT(resources.command_buffer, 1, &build_info);

	VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr};
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
	barrier.srcAccessMask = VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
	barrier.dstAccessMask = VK_ACCESS_2_MICROMAP_READ_BIT_EXT;
	VkDependencyInfo dependency_info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr};
	dependency_info.memoryBarrierCount = 1;
	dependency_info.pMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2(resources.command_buffer, &dependency_info);

	resources.micromap.vkCmdWriteMicromapsPropertiesEXT(
		resources.command_buffer, 1, &resources.source_micromap, VK_QUERY_TYPE_MICROMAP_COMPACTED_SIZE_EXT, resources.compacted_query_pool, 0);
	resources.micromap.vkCmdWriteMicromapsPropertiesEXT(
		resources.command_buffer, 1, &resources.source_micromap, VK_QUERY_TYPE_MICROMAP_SERIALIZATION_SIZE_EXT, resources.serialization_query_pool, 0);
	submit_and_wait(resources.queue, resources.command_buffer);

	destroy_buffer(vulkan, scratch_buffer);
}

static void serialize_and_copy_micromap(const vulkan_setup_t& vulkan, Resources& resources, VkDeviceSize compacted_size, VkDeviceSize serialization_size)
{
	resources.compacted_micromap = create_micromap(vulkan, resources, resources.compacted_storage_buffer, compacted_size, "compacted_opacity_micromap");

	resources.serialized_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		serialization_size + 512,
		nullptr,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	resources.serialized_address = get_aligned_device_address(vulkan, resources.serialized_buffer, 256);
	assert((resources.serialized_address.device_address & 255) == 0);

	VkCopyMicromapInfoEXT compact_info{VK_STRUCTURE_TYPE_COPY_MICROMAP_INFO_EXT, nullptr};
	compact_info.src = resources.source_micromap;
	compact_info.dst = resources.compacted_micromap;
	compact_info.mode = VK_COPY_MICROMAP_MODE_COMPACT_EXT;

	VkCopyMicromapToMemoryInfoEXT serialize_info{VK_STRUCTURE_TYPE_COPY_MICROMAP_TO_MEMORY_INFO_EXT, nullptr};
	serialize_info.src = resources.source_micromap;
	serialize_info.dst.deviceAddress = resources.serialized_address.device_address;
	serialize_info.mode = VK_COPY_MICROMAP_MODE_SERIALIZE_EXT;

	begin_commands(resources.command_buffer);
	resources.micromap.vkCmdCopyMicromapEXT(resources.command_buffer, &compact_info);
	resources.micromap.vkCmdCopyMicromapToMemoryEXT(resources.command_buffer, &serialize_info);
	submit_and_wait(resources.queue, resources.command_buffer);
}

static void deserialize_micromap(const vulkan_setup_t& vulkan, Resources& resources, VkDeviceSize size)
{
	resources.deserialized_micromap = create_micromap(vulkan, resources, resources.deserialized_storage_buffer, size, "deserialized_opacity_micromap");

	VkCopyMemoryToMicromapInfoEXT deserialize_info{VK_STRUCTURE_TYPE_COPY_MEMORY_TO_MICROMAP_INFO_EXT, nullptr};
	deserialize_info.src.deviceAddress = resources.serialized_address.device_address;
	deserialize_info.dst = resources.deserialized_micromap;
	deserialize_info.mode = VK_COPY_MICROMAP_MODE_DESERIALIZE_EXT;

	begin_commands(resources.command_buffer);
	resources.micromap.vkCmdCopyMemoryToMicromapEXT(resources.command_buffer, &deserialize_info);
	submit_and_wait(resources.queue, resources.command_buffer);
}

static void build_blas_with_micromap(const vulkan_setup_t& vulkan, Resources& resources, const VkMicromapUsageEXT& usage)
{
	VkAccelerationStructureTrianglesOpacityMicromapEXT opacity_info{
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT, nullptr
	};
	opacity_info.indexType = VK_INDEX_TYPE_NONE_KHR;
	opacity_info.baseTriangle = 0;
	opacity_info.usageCountsCount = 1;
	opacity_info.pUsageCounts = &usage;
	opacity_info.micromap = resources.deserialized_micromap;

	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry.geometry.triangles.pNext = &opacity_info;
	geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry.geometry.triangles.vertexData.deviceAddress = resources.vertex_buffer.address.deviceAddress;
	geometry.geometry.triangles.maxVertex = static_cast<uint32_t>(kVertices.size());
	geometry.geometry.triangles.vertexStride = sizeof(Vertex);
	geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geometry.geometry.triangles.indexData.deviceAddress = resources.index_buffer.address.deviceAddress;

	VkAccelerationStructureBuildGeometryInfoKHR build_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build_info.geometryCount = 1;
	build_info.pGeometries = &geometry;

	const uint32_t primitive_count = 1;
	VkAccelerationStructureBuildSizesInfoKHR build_sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	resources.accel.vkGetAccelerationStructureBuildSizesKHR(
		vulkan.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &primitive_count, &build_sizes);
	assert(build_sizes.accelerationStructureSize > 0);

	resources.blas_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		build_sizes.accelerationStructureSize,
		nullptr,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
	create_info.buffer = resources.blas_buffer.handle;
	create_info.size = build_sizes.accelerationStructureSize;
	create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	check(resources.accel.vkCreateAccelerationStructureKHR(vulkan.device, &create_info, nullptr, &resources.blas.handle));
	test_set_name(vulkan, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (uint64_t)resources.blas.handle, "opacity_micromap_blas");

	Buffer scratch_buffer{};
	if (build_sizes.buildScratchSize != 0)
	{
		scratch_buffer = acceleration_structures::prepare_buffer(
			vulkan,
			build_sizes.buildScratchSize,
			nullptr,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		scratch_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, scratch_buffer.handle);
	}

	build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info.dstAccelerationStructure = resources.blas.handle;
	build_info.scratchData.deviceAddress = scratch_buffer.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR range_info{};
	range_info.primitiveCount = primitive_count;
	VkAccelerationStructureBuildRangeInfoKHR* range_infos = &range_info;

	begin_commands(resources.command_buffer);
	resources.accel.vkCmdBuildAccelerationStructuresKHR(resources.command_buffer, 1, &build_info, &range_infos);
	submit_and_wait(resources.queue, resources.command_buffer);

	destroy_buffer(vulkan, scratch_buffer);
}

static void cleanup_resources(const vulkan_setup_t& vulkan, Resources& resources)
{
	if (resources.blas.handle != VK_NULL_HANDLE)
	{
		resources.accel.vkDestroyAccelerationStructureKHR(vulkan.device, resources.blas.handle, nullptr);
		resources.blas.handle = VK_NULL_HANDLE;
	}

	destroy_micromap(vulkan, resources.micromap, resources.deserialized_micromap);
	destroy_micromap(vulkan, resources.micromap, resources.compacted_micromap);
	destroy_micromap(vulkan, resources.micromap, resources.source_micromap);

	destroy_buffer(vulkan, resources.blas_buffer);
	destroy_buffer(vulkan, resources.deserialized_storage_buffer);
	destroy_buffer(vulkan, resources.serialized_buffer);
	destroy_buffer(vulkan, resources.compacted_storage_buffer);
	destroy_buffer(vulkan, resources.source_storage_buffer);
	destroy_buffer(vulkan, resources.index_buffer);
	destroy_buffer(vulkan, resources.vertex_buffer);
	destroy_buffer(vulkan, resources.triangle_buffer);
	destroy_buffer(vulkan, resources.data_buffer);

	if (resources.compacted_query_pool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(vulkan.device, resources.compacted_query_pool, nullptr);
		resources.compacted_query_pool = VK_NULL_HANDLE;
	}

	if (resources.serialization_query_pool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(vulkan.device, resources.serialization_query_pool, nullptr);
		resources.serialization_query_pool = VK_NULL_HANDLE;
	}

	if (resources.command_pool != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(vulkan.device, resources.command_pool, nullptr);
		resources.command_pool = VK_NULL_HANDLE;
	}
}
}

int main(int argc, char** argv)
{
	VkPhysicalDeviceOpacityMicromapFeaturesEXT micromap_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT, nullptr};
	micromap_features.micromap = VK_TRUE;

	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
	reqs.bufferDeviceAddress = true;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&micromap_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_opacity_micromap", reqs);

	VkPhysicalDeviceOpacityMicromapFeaturesEXT queried_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT, nullptr};
	VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &queried_features};
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &features2);
	if (!queried_features.micromap)
	{
		test_done(vulkan);
		return 77;
	}

	VkPhysicalDeviceOpacityMicromapPropertiesEXT queried_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT, nullptr};
	VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &queried_properties};
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties2);
	if (queried_properties.maxOpacity2StateSubdivisionLevel == 0)
	{
		test_done(vulkan);
		return 77;
	}

	Resources resources{};
	init_resources(vulkan, resources);

	const VkMicromapUsageEXT usage = { 1, 0, VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT };

	bench_start_iteration(vulkan.bench);
	VkMicromapBuildSizesInfoEXT build_sizes{VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT, nullptr};
	build_source_micromap(vulkan, resources, usage, build_sizes);

	VkDeviceSize compacted_size = 0;
	VkDeviceSize serialization_size = 0;
	check(vkGetQueryPoolResults(vulkan.device, resources.compacted_query_pool, 0, 1, sizeof(compacted_size), &compacted_size, sizeof(compacted_size),
	                            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
	check(vkGetQueryPoolResults(vulkan.device, resources.serialization_query_pool, 0, 1, sizeof(serialization_size), &serialization_size,
	                            sizeof(serialization_size), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
	assert(compacted_size > 0);
	assert(serialization_size > 0);

	serialize_and_copy_micromap(vulkan, resources, compacted_size, serialization_size);

	void* mapped = nullptr;
	check(vkMapMemory(vulkan.device, resources.serialized_buffer.memory, 0, resources.serialized_address.offset + serialization_size, 0, &mapped));
	VkMicromapVersionInfoEXT version_info{VK_STRUCTURE_TYPE_MICROMAP_VERSION_INFO_EXT, nullptr};
	version_info.pVersionData = static_cast<const uint8_t*>(mapped) + resources.serialized_address.offset;
	VkAccelerationStructureCompatibilityKHR compatibility = VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
	resources.micromap.vkGetDeviceMicromapCompatibilityEXT(vulkan.device, &version_info, &compatibility);
	vkUnmapMemory(vulkan.device, resources.serialized_buffer.memory);
	assert(compatibility == VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR);

	deserialize_micromap(vulkan, resources, build_sizes.micromapSize);
	test_marker_mention(vulkan, "Built and deserialized opacity micromaps", VK_OBJECT_TYPE_MICROMAP_EXT, (uint64_t)resources.deserialized_micromap);

	build_blas_with_micromap(vulkan, resources, usage);
	bench_stop_iteration(vulkan.bench);

	cleanup_resources(vulkan, resources);
	test_done(vulkan);
	return 0;
}
