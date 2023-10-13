#include "vulkan_common.h"
#include <inttypes.h>

static uint32_t as_build_count = 1;
static bool as_batch_build = false;
static bool as_host_build = false;

PFN_vkCreateAccelerationStructureKHR ttCreateAccelerationStructureKHR;
PFN_vkGetAccelerationStructureBuildSizesKHR ttGetAccelerationStructureBuildSizesKHR;
PFN_vkGetAccelerationStructureDeviceAddressKHR ttGetAccelerationStructureDeviceAddressKHR;
PFN_vkCmdBuildAccelerationStructuresKHR ttCmdBuildAccelerationStructuresKHR;
PFN_vkBuildAccelerationStructuresKHR ttBuildAccelerationStructuresKHR;
PFN_vkDestroyAccelerationStructureKHR ttDestroyAccelerationStructure;
PFN_vkGetBufferDeviceAddress ttGetBufferDeviceAddress; 

struct Vertex
{
	float pos[3];
};

static std::vector<Vertex> vertices = {
	{{1.0f, 1.0f, 0.0f}}, 
	{{-1.0f, 1.0f, 0.0f}}, 
	{{0.0f, -1.0f, 0.0f}}
};

static  std::vector<uint32_t> indices = {0, 1, 2};
static  uint32_t index_count = static_cast<uint32_t>(indices.size());

struct Buffer
{
	VkBuffer handle;
	VkDeviceMemory memory;
	VkDeviceOrHostAddressConstKHR adress;
};

struct BLAS
{
	VkAccelerationStructureKHR handle;
	VkDeviceOrHostAddressConstKHR adress;
	Buffer buffer;
};

struct Resources
{
	std::vector<BLAS> blas;
	std::vector<Buffer> vertex_buffers;
	std::vector<Buffer> index_buffers;
	VkQueue queue;
	VkCommandPool command_pool;
};

static void show_usage() 
{
	printf("-c/--count N           Build N acceleration structures, default is %u\n", as_build_count);
	printf("-b/--batch             Batch build acceleration structures, default %s\n", as_batch_build ? "true" : "false");
	printf("-hb/--host-build       Build acceleration structures on host, default %s\n", as_host_build ? "true" : "false");
}

static bool test_cmdopt(int &i, int argc, char **argv, vulkan_req_t &reqs)
{
	if (match(argv[i], "-c", "--count"))
	{
		as_build_count = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-b", "--batch"))
	{
		as_batch_build = true;
		return true;
	}
	else if (match(argv[i],"-hb","--host-build"))
	{
		as_host_build = true;
		return true;
	}
	return false;
}

Buffer prepare_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, void *data, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties)
{
	VkBufferCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create_info.pNext = nullptr;
	create_info.usage = usage;
	create_info.size = size;
	Buffer buffer{};
	check(vkCreateBuffer(vulkan.device, &create_info, nullptr, &buffer.handle));

	VkMemoryRequirements memory_requirements{};
	vkGetBufferMemoryRequirements(vulkan.device, buffer.handle, &memory_requirements);
	VkMemoryAllocateInfo memory_allocate_info{};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, memory_properties);
	
	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) 
	{
		VkMemoryAllocateFlagsInfoKHR allocation_flags_info{};
		allocation_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
		allocation_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		memory_allocate_info.pNext = &allocation_flags_info;
	}

	check(vkAllocateMemory(vulkan.device, &memory_allocate_info, nullptr, &buffer.memory));

	if (data)
	{
		void *mapped;
		check(vkMapMemory(vulkan.device, buffer.memory, 0, size, 0, &mapped));
		memcpy(mapped, data, size);
		vkUnmapMemory(vulkan.device, buffer.memory);
	}
	check(vkBindBufferMemory(vulkan.device, buffer.handle, buffer.memory, 0));
	return buffer;
}

VkDeviceAddress get_buffer_device_adress(VkDevice device, VkBuffer buffer)
{
	VkBufferDeviceAddressInfo buffer_device_adress_info{};
	buffer_device_adress_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	buffer_device_adress_info.pNext = nullptr;
	buffer_device_adress_info.buffer = buffer;
	return ttGetBufferDeviceAddress(device, &buffer_device_adress_info);
}

void prepare(const vulkan_setup_t& vulkan, Resources & resources)
{
	ttCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCreateAccelerationStructureKHR"));
	assert(ttCreateAccelerationStructureKHR);

	ttGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(vulkan.device, "vkGetAccelerationStructureBuildSizesKHR"));
	assert(ttGetAccelerationStructureBuildSizesKHR);
		
	ttCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCmdBuildAccelerationStructuresKHR"));
	assert(ttCmdBuildAccelerationStructuresKHR);

	ttBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(vulkan.device, "vkBuildAccelerationStructuresKHR"));
	assert(ttBuildAccelerationStructuresKHR);

	ttGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(vulkan.device, "vkGetAccelerationStructureDeviceAddressKHR"));
	assert(ttGetAccelerationStructureDeviceAddressKHR);
	
	ttDestroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(vulkan.device, "vkDestroyAccelerationStructureKHR"));
	assert(ttDestroyAccelerationStructure);

	ttGetBufferDeviceAddress = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(vkGetDeviceProcAddr(vulkan.device, "vkGetBufferDeviceAddress"));
	assert(ttGetBufferDeviceAddress);
	
	VkCommandPoolCreateInfo command_pool_create_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	command_pool_create_info.queueFamilyIndex = 0; // TODO Make sure that this points to compute
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkResult command_pool_create_result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, nullptr, &resources.command_pool);
	check(command_pool_create_result);

	vkGetDeviceQueue(vulkan.device, 0, 0, &resources.queue);
	resources.blas = std::vector<BLAS>(as_build_count);
	resources.vertex_buffers = std::vector<Buffer>(as_build_count);
	resources.index_buffers = std::vector<Buffer>(as_build_count);

	// Create the minimal buffers for the acceleration structure build
	for(std::size_t index = 0; index < as_build_count; ++index)
	{
		resources.vertex_buffers[index] = prepare_buffer(
			vulkan, 
			vertices.size() * sizeof(Vertex), 
			vertices.data(), 
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		resources.vertex_buffers[index].adress.deviceAddress = get_buffer_device_adress(vulkan.device, resources.vertex_buffers[index].handle);

		resources.index_buffers[index] = prepare_buffer(
			vulkan, 
			indices.size() * sizeof(uint32_t), 
			indices.data(), 
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		resources.index_buffers[index].adress.deviceAddress = get_buffer_device_adress(vulkan.device, resources.index_buffers[index].handle);
	}
}

void build_acceleration_structures(const vulkan_setup_t &vulkan, Resources & resources)
{
	const uint32_t numTriangles = 1;
	std::vector<Buffer> scratch_buffers(as_build_count);
	std::vector<VkAccelerationStructureGeometryKHR> as_geometries(as_build_count);
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> build_geometry_infos(as_build_count);
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> build_range_infos(as_build_count);
	
	// Prepare the information for all bottom level acceleration stuctures 
	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		as_geometries[as_index].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		as_geometries[as_index].flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		as_geometries[as_index].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		as_geometries[as_index].geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		as_geometries[as_index].geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		as_geometries[as_index].geometry.triangles.vertexData = resources.vertex_buffers[as_index].adress;
		as_geometries[as_index].geometry.triangles.maxVertex = 3;
		as_geometries[as_index].geometry.triangles.vertexStride = sizeof(Vertex);
		as_geometries[as_index].geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		as_geometries[as_index].geometry.triangles.indexData = resources.index_buffers[as_index].adress;
		
		build_geometry_infos[as_index].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		build_geometry_infos[as_index].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		build_geometry_infos[as_index].flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		build_geometry_infos[as_index].geometryCount = 1;
		build_geometry_infos[as_index].pGeometries = &as_geometries[as_index];
		
		VkAccelerationStructureBuildSizesInfoKHR build_sizes_info{};
		build_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

		ttGetAccelerationStructureBuildSizesKHR(
			vulkan.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_OR_DEVICE_KHR, &build_geometry_infos[as_index], &numTriangles, &build_sizes_info
		);

		resources.blas[as_index].buffer = prepare_buffer(
			vulkan, build_sizes_info.accelerationStructureSize,
			nullptr,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR
		);

		VkAccelerationStructureCreateInfoKHR create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		create_info.buffer = resources.blas[as_index].buffer.handle;
		create_info.size = build_sizes_info.accelerationStructureSize;
		create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	
		check(ttCreateAccelerationStructureKHR(vulkan.device, &create_info, nullptr, &resources.blas[as_index].handle));

		scratch_buffers[as_index] = prepare_buffer(
			vulkan, 
			build_sizes_info.buildScratchSize, 
			nullptr, 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		VkBufferDeviceAddressInfoKHR scratch_buffer_device_adress_info{};
		scratch_buffer_device_adress_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		scratch_buffer_device_adress_info.buffer = scratch_buffers[as_index].handle;
		scratch_buffers[as_index].adress.deviceAddress = ttGetBufferDeviceAddress(vulkan.device, &scratch_buffer_device_adress_info);

		build_geometry_infos[as_index].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build_geometry_infos[as_index].dstAccelerationStructure = resources.blas[as_index].handle;
		build_geometry_infos[as_index].scratchData.deviceAddress = scratch_buffers[as_index].adress.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR* ac_build_range_info = new VkAccelerationStructureBuildRangeInfoKHR();
		ac_build_range_info->primitiveCount = numTriangles;
		ac_build_range_info->primitiveOffset = 0;
		build_range_infos[as_index] = { ac_build_range_info };
	}

	if(!as_host_build)
	{
		VkCommandBufferAllocateInfo command_buffer_allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
		command_buffer_allocate_info.commandPool = resources.command_pool;
		command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		command_buffer_allocate_info.commandBufferCount = 1;

		VkCommandBuffer command_buffer{};
		check(vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &command_buffer));
		VkCommandBufferBeginInfo command_buffer_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
		check(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

		if(as_batch_build)
		{
			ttCmdBuildAccelerationStructuresKHR(command_buffer, as_build_count, build_geometry_infos.data(), build_range_infos.data());
		}
		else
		{
			for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
			{
				ttCmdBuildAccelerationStructuresKHR(command_buffer, 1, &build_geometry_infos[as_index], build_range_infos.data());
			}
		}

		check(vkEndCommandBuffer(command_buffer));	
		VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &command_buffer;
		check(vkQueueSubmit(resources.queue, 1, &submitInfo, nullptr));
		check(vkQueueWaitIdle(resources.queue));
		vkFreeCommandBuffers(vulkan.device, resources.command_pool, 1, &command_buffer);
	}
	else
	{
		if(as_batch_build)
		{
				ttBuildAccelerationStructuresKHR(vulkan.device, VK_NULL_HANDLE, as_build_count, build_geometry_infos.data(), build_range_infos.data());
		}
		else
		{
			for(uint32_t as_index = 0; as_index < as_build_count; ++as_index){
				ttBuildAccelerationStructuresKHR(vulkan.device, VK_NULL_HANDLE, 1, &build_geometry_infos[as_index], build_range_infos.data());
			}
		}
	}
	
	VkAccelerationStructureDeviceAddressInfoKHR blas_device_adress_info{};
	blas_device_adress_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	
	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		blas_device_adress_info.accelerationStructure = resources.blas[as_index].handle;
		resources.blas[as_index].adress.deviceAddress = ttGetAccelerationStructureDeviceAddressKHR(vulkan.device, &blas_device_adress_info);
	}
	
	// Cleanup after building
	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		delete build_range_infos[as_index];
		vkDestroyBuffer(vulkan.device, scratch_buffers[as_index].handle, nullptr);
		vkFreeMemory(vulkan.device, scratch_buffers[as_index].memory, nullptr);
	}

}

int main(int argc, char **argv)
{
	vulkan_req_t reqs;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accfeats = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, nullptr, VK_TRUE };
	reqs.device_extensions.push_back("VK_KHR_acceleration_structure");
	reqs.device_extensions.push_back("VK_KHR_deferred_host_operations");
	reqs.bufferDeviceAddress = true;
	reqs.extension_features = (VkBaseInStructure*)&accfeats;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.queues = 1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_as_2", reqs);

	if(as_host_build)
	{
		assert(accfeats.accelerationStructureHostCommands && "Host build is not supported by the GPU");
	}

	Resources resources{};
	prepare(vulkan, resources);
	build_acceleration_structures(vulkan, resources);

	vkDestroyCommandPool(vulkan.device, resources.command_pool,nullptr);

	for(BLAS & blas : resources.blas)
	{
		vkFreeMemory(vulkan.device, blas.buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, blas.buffer.handle, nullptr);
		ttDestroyAccelerationStructure(vulkan.device, blas.handle, nullptr);
	}

	for(Buffer & buffer : resources.vertex_buffers)
	{
		vkFreeMemory(vulkan.device, buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, buffer.handle, nullptr);
	}

	for(Buffer & buffer : resources.index_buffers)
	{
		vkFreeMemory(vulkan.device, buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, buffer.handle, nullptr);
	}

	test_done(vulkan);
	return 0;
}
