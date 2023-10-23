#include "vulkan_common.h"
#include <cstdint>

using Buffer = acceleration_structures::Buffer;
using BLAS = acceleration_structures::BLAS;

static uint32_t as_build_count = 1;
static bool as_host_build = false;

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
	std::vector<BLAS> opt_bl_acc_structures;
	std::vector<Buffer> vertex_buffers;
	std::vector<Buffer> index_buffers;
	std::vector<Buffer> original_blas_buffers;
	std::vector<Buffer> optimized_blas_buffers;
	VkQueue queue{ VK_NULL_HANDLE };
	VkCommandPool command_pool{ VK_NULL_HANDLE};
	VkQueryPool query_pool{ VK_NULL_HANDLE };
};

static void show_usage()
{
	printf("Test the compacting build of bottom level acceleration structure\n");
	printf("-c/--count N           Build N acceleration structures, default is %u\n", as_build_count);
	printf("-hb/--host-build       Build acceleration structures on host, default %s\n", as_host_build ? "true" : "false");
}

static bool test_cmdopt(int &i, int argc, char **argv, vulkan_req_t &reqs)
{
	if (match(argv[i], "-c", "--count"))
	{
		as_build_count = get_arg(argv, ++i, argc);
		return true;
	}
	if(match(argv[i], "-hb", "--host-build"))
	{
		as_host_build = true;
		return true;
	}
	return false;
}

void prepare_test_resources(const vulkan_setup_t& vulkan, Resources & resources)
{
	resources.functions = acceleration_structures::query_acceleration_structure_functions(vulkan.device);

	VkCommandPoolCreateInfo command_pool_create_info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_create_info.queueFamilyIndex = 0; // TODO Make sure that this points to compute
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkResult command_pool_create_result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, nullptr, &resources.command_pool);
	check(command_pool_create_result);

	vkGetDeviceQueue(vulkan.device, 0, 0, &resources.queue);

	VkQueryPoolCreateInfo query_pool_create_info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr };
	query_pool_create_info.queryCount = as_build_count;
	query_pool_create_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
	check(vkCreateQueryPool(vulkan.device, &query_pool_create_info, nullptr, &resources.query_pool));

	resources.bl_acc_structures = std::vector<BLAS>(as_build_count);
	resources.opt_bl_acc_structures = std::vector<BLAS>(as_build_count);
	resources.vertex_buffers = std::vector<Buffer>(as_build_count);
	resources.index_buffers = std::vector<Buffer>(as_build_count);
	resources.original_blas_buffers = std::vector<Buffer>(as_build_count);
	resources.optimized_blas_buffers = std::vector<Buffer>(as_build_count);

	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		resources.vertex_buffers[as_index] = acceleration_structures::prepare_buffer(
			vulkan,
			vertices.size() * sizeof(Vertex),
			vertices.data(),
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		resources.vertex_buffers[as_index].address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.vertex_buffers[as_index].handle);

		resources.index_buffers[as_index] =  acceleration_structures::prepare_buffer(
			vulkan,
			indices.size() * sizeof(uint32_t),
			indices.data(),
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		resources.index_buffers[as_index].address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.index_buffers[as_index].handle);
	}
}

void free_test_resources(const vulkan_setup_t& vulkan, Resources & resources){
    vkDestroyQueryPool(vulkan.device, resources.query_pool, nullptr);
	vkDestroyCommandPool(vulkan.device, resources.command_pool, nullptr);

    for(uint32_t as_index = 0; as_index < as_build_count; ++as_index){
        resources.functions.vkDestroyAccelerationStructure(vulkan.device, resources.bl_acc_structures[as_index].handle, nullptr);
        vkFreeMemory(vulkan.device, resources.original_blas_buffers[as_index].memory, nullptr);
        vkDestroyBuffer(vulkan.device, resources.original_blas_buffers[as_index].handle, nullptr);

        resources.functions.vkDestroyAccelerationStructure(vulkan.device, resources.opt_bl_acc_structures[as_index].handle, nullptr);
	    vkFreeMemory(vulkan.device, resources.optimized_blas_buffers[as_index].memory, nullptr);
	    vkDestroyBuffer(vulkan.device, resources.optimized_blas_buffers[as_index].handle, nullptr);

	    vkFreeMemory(vulkan.device, resources.vertex_buffers[as_index].memory, nullptr);
	    vkDestroyBuffer(vulkan.device, resources.vertex_buffers[as_index].handle, nullptr);

	    vkFreeMemory(vulkan.device, resources.index_buffers[as_index].memory, nullptr);
	    vkDestroyBuffer(vulkan.device, resources.index_buffers[as_index].handle, nullptr);
    }
}

void optimize_acceleration_structure(const vulkan_setup_t& vulkan, Resources & resources)
{
	const uint32_t num_triangles = 1;
	std::vector<Buffer> as_scratch_buffers(as_build_count);
	std::vector<VkAccelerationStructureGeometryKHR> as_geometries(as_build_count);
	std::vector<VkAccelerationStructureBuildSizesInfoKHR> as_build_size_infos(as_build_count);
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> as_build_geometry_infos(as_build_count);
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> as_build_range_infos(as_build_count);
	std::vector<VkAccelerationStructureKHR> as_handles(as_build_count);

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
        as_build_geometry_infos[as_index].pNext = nullptr;
        as_build_geometry_infos[as_index].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        as_build_geometry_infos[as_index].flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        as_build_geometry_infos[as_index].geometryCount = 1;
        as_build_geometry_infos[as_index].pGeometries = &as_geometries[as_index];

        as_build_size_infos[as_index].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        as_build_size_infos[as_index].pNext = nullptr;
        resources.functions.vkGetAccelerationStructureBuildSizesKHR(
            vulkan.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_OR_DEVICE_KHR, &as_build_geometry_infos[as_index], &num_triangles, &as_build_size_infos[as_index]
        );

        resources.original_blas_buffers[as_index] = acceleration_structures::prepare_buffer(
	    	vulkan, as_build_size_infos[as_index].accelerationStructureSize,
	    	nullptr,
	    	VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    	VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR
	    );

        VkAccelerationStructureCreateInfoKHR as_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
        as_create_info.buffer = resources.original_blas_buffers[as_index].handle;
        as_create_info.size = as_build_size_infos[as_index].accelerationStructureSize;
        as_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &as_create_info, nullptr, &resources.bl_acc_structures[as_index].handle));
        as_handles[as_index] = resources.bl_acc_structures[as_index].handle;

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

        auto* build_range = new VkAccelerationStructureBuildRangeInfoKHR();
        build_range->primitiveCount = num_triangles;
        build_range->primitiveOffset = 0;
        as_build_range_infos[as_index] = build_range;
    }

	std::vector<VkDeviceSize> compacted_sizes(as_build_count);
	if(as_host_build)
	{
		resources.functions.vkBuildAccelerationStructuresKHR(vulkan.device, VK_NULL_HANDLE, as_build_count, as_build_geometry_infos.data(), as_build_range_infos.data());
		resources.functions.vkWriteAccelerationStructuresPropertiesKHR(
			vulkan.device, as_build_count, as_handles.data(), VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, sizeof(VkDeviceSize)*as_build_count, compacted_sizes.data(), 0
		);
	}
	else
	{
		VkCommandBufferAllocateInfo command_buffer_allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
		command_buffer_allocate_info.commandPool = resources.command_pool;
		command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		command_buffer_allocate_info.commandBufferCount = 1;

		VkCommandBuffer command_buffer{};
		check(vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &command_buffer));
		VkCommandBufferBeginInfo command_buffer_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
		check(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

		vkCmdResetQueryPool(command_buffer, resources.query_pool, 0, as_build_count);

		resources.functions.vkCmdBuildAccelerationStructuresKHR(command_buffer, as_build_count, as_build_geometry_infos.data(), as_build_range_infos.data());

		VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr};
		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier , 0, nullptr, 0, nullptr);

		resources.functions.vkCmdWriteAccelerationStructuresPropertiesKHR(command_buffer, as_build_count, as_handles.data(), VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, resources.query_pool , 0);

		check(vkEndCommandBuffer(command_buffer));

		VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &command_buffer;
		check(vkQueueSubmit(resources.queue, 1, &submitInfo, nullptr));
		check(vkQueueWaitIdle(resources.queue));
		check(vkGetQueryPoolResults(vulkan.device, resources.query_pool, 0, as_build_count, sizeof(VkDeviceSize), compacted_sizes.data(), sizeof(VkDeviceSize), VK_QUERY_RESULT_WAIT_BIT));
		vkFreeCommandBuffers(vulkan.device, resources.command_pool, 1, &command_buffer);
	}

	std::vector<VkCopyAccelerationStructureInfoKHR> copy_infos;
	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		assert(compacted_sizes[as_index] <= as_build_size_infos[as_index].accelerationStructureSize);
		if(compacted_sizes[as_index] == as_build_size_infos[as_index].accelerationStructureSize)
		{
			continue;
		}

		resources.optimized_blas_buffers[as_index] = acceleration_structures::prepare_buffer(
			vulkan, compacted_sizes[as_index],
			nullptr,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR
		);
		resources.optimized_blas_buffers[as_index].address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.optimized_blas_buffers[as_index].handle);

		VkAccelerationStructureCreateInfoKHR optimized_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
		optimized_create_info.buffer = resources.optimized_blas_buffers[as_index].handle;
		optimized_create_info.size = compacted_sizes[as_index];
		optimized_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &optimized_create_info, nullptr, &resources.opt_bl_acc_structures[as_index].handle));

		VkCopyAccelerationStructureInfoKHR copy_info{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR, nullptr};
		copy_info.src = resources.bl_acc_structures[as_index].handle;
		copy_info.dst = resources.opt_bl_acc_structures[as_index].handle;
		copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
		copy_infos.push_back(copy_info);
	}

	if(as_host_build)
	{
		for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
		{
			resources.functions.vkCopyAccelerationStructureKHR(vulkan.device, VK_NULL_HANDLE, &copy_infos[as_index]);
		}
	}
	else
	{
		VkCommandBufferAllocateInfo command_buffer_allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
		command_buffer_allocate_info.commandPool = resources.command_pool;
		command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		command_buffer_allocate_info.commandBufferCount = 1;

		VkCommandBuffer command_buffer{};
		check(vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &command_buffer));
		VkCommandBufferBeginInfo command_buffer_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
		check(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

		for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
		{
			resources.functions.vkCmdCopyAccelerationStructureKHR(command_buffer, &copy_infos[as_index]);
		}

		check(vkEndCommandBuffer(command_buffer));

		VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &command_buffer;
		check(vkQueueSubmit(resources.queue, 1, &submitInfo, nullptr));
		check(vkQueueWaitIdle(resources.queue));
		vkFreeCommandBuffers(vulkan.device, resources.command_pool, 1, &command_buffer);
	}

	for(uint32_t as_index = 0; as_index < as_build_count; ++as_index)
	{
		delete as_build_range_infos[as_index];
		vkDestroyBuffer(vulkan.device, as_scratch_buffers[as_index].handle, nullptr);
		vkFreeMemory(vulkan.device, as_scratch_buffers[as_index].memory, nullptr);
	}
}

int main(int argc, char** argv)
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
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_as_3", reqs);

	if(as_host_build)
	{
		assert(accfeats.accelerationStructureHostCommands && "Host build is not supported by the GPU");
	}

	Resources resources{};
	prepare_test_resources(vulkan, resources);
	optimize_acceleration_structure(vulkan, resources);
	free_test_resources(vulkan, resources);

	test_done(vulkan);
	return 0;
}