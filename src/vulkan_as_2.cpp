#include "vulkan_common.h"
#include <inttypes.h>

using Buffer = acceleration_structures::Buffer;
using BLAS = acceleration_structures::BLAS;

static uint32_t as_build_count = 1;
static bool as_batch_build = false;
static bool as_host_build = false;
static bool as_packed = false;

struct Vertex
{
	float pos[3];
};

static std::vector<Vertex> vertices = {
	{{1.0f, 1.0f, 0.0f}},
	{{-1.0f, 1.0f, 0.0f}},
	{{0.0f, -1.0f, 0.0f}}
};

static std::vector<uint32_t> indices = {0, 1, 2};
static uint32_t index_count = static_cast<uint32_t>(indices.size());

struct Resources
{
	acceleration_structures::functions functions;
	std::vector<BLAS> bl_acc_structures;
	std::vector<Buffer> buffers;
	std::vector<Buffer> vertex_buffers;
	std::vector<Buffer> index_buffers;
	VkQueue queue{ VK_NULL_HANDLE };
	VkCommandPool command_pool{ VK_NULL_HANDLE };
};

static void show_usage()
{
	printf("Test the building of bottom level acceleration structures\n");
	printf("-c/--count N           Build N acceleration structures, default is %u\n", as_build_count);
	printf("-b/--batch             Batch build acceleration structures, default %s\n", as_batch_build ? "true" : "false");
	printf("-hb/--host-build       Build acceleration structures on host, default %s\n", as_host_build ? "true" : "false");
	printf("-p/--packed            Create acceleration structures in one packed buffer, default %s\n", as_packed ? "true" : "false");
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
	else if (match(argv[i],"-p","--packed"))
	{
		as_packed = true;
		return true;
	}
	return false;
}

void prepare_test_resources(const vulkan_setup_t& vulkan, Resources & resources)
{
	resources.functions = acceleration_structures::query_acceleration_structure_functions(vulkan.device);

	VkCommandPoolCreateInfo command_pool_create_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	command_pool_create_info.queueFamilyIndex = 0; // TODO Make sure that this points to compute
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkResult command_pool_create_result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, nullptr, &resources.command_pool);
	check(command_pool_create_result);

	vkGetDeviceQueue(vulkan.device, 0, 0, &resources.queue);
	resources.bl_acc_structures = std::vector<BLAS>(as_build_count);
	resources.vertex_buffers = std::vector<Buffer>(as_build_count);
	resources.index_buffers = std::vector<Buffer>(as_build_count);

	for(std::size_t index = 0; index < as_build_count; ++index)
	{
		resources.vertex_buffers[index] = acceleration_structures::prepare_buffer(
			vulkan,
			vertices.size() * sizeof(Vertex),
			vertices.data(),
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		resources.vertex_buffers[index].address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.vertex_buffers[index].handle);

		resources.index_buffers[index] = acceleration_structures::prepare_buffer(
			vulkan,
			indices.size() * sizeof(uint32_t),
			indices.data(),
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		resources.index_buffers[index].address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.index_buffers[index].handle);
	}
}

void free_test_resources(const vulkan_setup_t& vulkan, Resources & resources){
	vkDestroyCommandPool(vulkan.device, resources.command_pool, nullptr);

	for(BLAS & blas : resources.bl_acc_structures)
	{
		resources.functions.vkDestroyAccelerationStructure(vulkan.device, blas.handle, nullptr);
	}

	for(auto & buffer: resources.buffers)
	{
		vkFreeMemory(vulkan.device, buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, buffer.handle, nullptr);
	}

	for(auto & buffer: resources.vertex_buffers)
	{
		vkFreeMemory(vulkan.device, buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, buffer.handle, nullptr);
	}

	for(auto & buffer: resources.index_buffers)
	{
		vkFreeMemory(vulkan.device, buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, buffer.handle, nullptr);
	}
}

void build_acceleration_structures(const vulkan_setup_t &vulkan, Resources & resources)
{
	const uint32_t num_triangles = 1;
	std::vector<Buffer> as_scratch_buffers(as_build_count);
	std::vector<VkAccelerationStructureGeometryKHR> as_geometries(as_build_count);
	std::vector<VkAccelerationStructureBuildSizesInfoKHR> as_build_size_infos(as_build_count);
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> as_build_geometry_infos(as_build_count);

	// Record the build sizes for acceleration structures
	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		as_geometries[as_index].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		as_geometries[as_index].pNext = nullptr;
		as_geometries[as_index].flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		as_geometries[as_index].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		as_geometries[as_index].geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		as_geometries[as_index].geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		as_geometries[as_index].geometry.triangles.vertexData = resources.vertex_buffers[as_index].address;
		as_geometries[as_index].geometry.triangles.maxVertex = 3;
		as_geometries[as_index].geometry.triangles.vertexStride = sizeof(Vertex);
		as_geometries[as_index].geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		as_geometries[as_index].geometry.triangles.indexData = resources.index_buffers[as_index].address;

		as_build_geometry_infos[as_index].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		as_build_geometry_infos[as_index].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		as_build_geometry_infos[as_index].flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		as_build_geometry_infos[as_index].geometryCount = 1;
		as_build_geometry_infos[as_index].pGeometries = &as_geometries[as_index];

		as_build_size_infos[as_index].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

		resources.functions.vkGetAccelerationStructureBuildSizesKHR(
			vulkan.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_OR_DEVICE_KHR, &as_build_geometry_infos[as_index], &num_triangles, &as_build_size_infos[as_index]
		);
	}

	std::vector<VkAccelerationStructureCreateInfoKHR> as_create_infos(as_build_count);
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> as_build_range_infos(as_build_count);

	// If building acceleration structures tightly packed, allocate one buffer and calculate offsets for structures
	// Record the creation information
	if(as_packed)
	{
		resources.buffers.resize(1);
		VkDeviceSize packed_blas_buffer_size = 0;
		for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
		{
			packed_blas_buffer_size += as_build_size_infos[as_index].accelerationStructureSize;
			// VkAccelerationStructureCreateInfoKHR requires offsets in the bufffer to be multiples of 256
			packed_blas_buffer_size += packed_blas_buffer_size % 256;
		}

		resources.buffers.front() = acceleration_structures::prepare_buffer(
			vulkan,
			packed_blas_buffer_size,
			nullptr,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR
		);

		VkDeviceSize offset = 0;
		for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
		{
			as_create_infos[as_index].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
			as_create_infos[as_index].buffer = resources.buffers.front().handle;
			as_create_infos[as_index].size = as_build_size_infos[as_index].accelerationStructureSize;
			as_create_infos[as_index].offset = offset;
			as_create_infos[as_index].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			offset += as_build_size_infos[as_index].accelerationStructureSize + as_build_size_infos[as_index].accelerationStructureSize % 256;
		}
	}
	// If not building packed structures, allocate a dedicated buffer for each structures
	// Record the creation information
	else
	{
		resources.buffers.resize(as_build_count);
		for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
		{
			resources.buffers[as_index] = acceleration_structures::prepare_buffer(
				vulkan, as_build_size_infos[as_index].accelerationStructureSize,
				nullptr,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR
			);

			as_create_infos[as_index].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
			as_create_infos[as_index].buffer = resources.buffers[as_index].handle;
			as_create_infos[as_index].size = as_build_size_infos[as_index].accelerationStructureSize;
			as_create_infos[as_index].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		}
	}

	// Actually create acceleration structures and allocate scratch buffer for build
	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &as_create_infos[as_index], nullptr, &resources.bl_acc_structures[as_index].handle));

		as_scratch_buffers[as_index] = acceleration_structures::prepare_buffer(
			vulkan,
			as_build_size_infos[as_index].buildScratchSize,
			nullptr,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		as_scratch_buffers[as_index].address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, as_scratch_buffers[as_index].handle);

		as_build_geometry_infos[as_index].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		as_build_geometry_infos[as_index].dstAccelerationStructure = resources.bl_acc_structures[as_index].handle;
		as_build_geometry_infos[as_index].scratchData.deviceAddress = as_scratch_buffers[as_index].address.deviceAddress;

		auto as_build_range_info = new VkAccelerationStructureBuildRangeInfoKHR();
		as_build_range_info->primitiveCount = num_triangles;
		as_build_range_info->primitiveOffset = 0;
		as_build_range_infos[as_index] = { as_build_range_info };
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
			resources.functions.vkCmdBuildAccelerationStructuresKHR(command_buffer, as_build_count, as_build_geometry_infos.data(), as_build_range_infos.data());
		}
		else
		{
			for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
			{
				resources.functions.vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &as_build_geometry_infos[as_index], as_build_range_infos.data());
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
			resources.functions.vkBuildAccelerationStructuresKHR(vulkan.device, VK_NULL_HANDLE, as_build_count, as_build_geometry_infos.data(), as_build_range_infos.data());
		}
		else
		{
			for(uint32_t as_index = 0; as_index < as_build_count; ++as_index){
				resources.functions.vkBuildAccelerationStructuresKHR(vulkan.device, VK_NULL_HANDLE, 1, &as_build_geometry_infos[as_index], as_build_range_infos.data());
			}
		}
	}

	VkAccelerationStructureDeviceAddressInfoKHR blas_device_adress_info{};
	blas_device_adress_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;

	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		blas_device_adress_info.accelerationStructure = resources.bl_acc_structures[as_index].handle;
		resources.bl_acc_structures[as_index].address.deviceAddress = resources.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &blas_device_adress_info);
	}

	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		delete as_build_range_infos[as_index];
		vkDestroyBuffer(vulkan.device, as_scratch_buffers[as_index].handle, nullptr);
		vkFreeMemory(vulkan.device, as_scratch_buffers[as_index].memory, nullptr);
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
	prepare_test_resources(vulkan, resources);
	build_acceleration_structures(vulkan, resources);
	free_test_resources(vulkan, resources);

	test_done(vulkan);
	return 0;
}
