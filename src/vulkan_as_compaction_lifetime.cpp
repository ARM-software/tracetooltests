#include "vulkan_common.h"

//	Contains the shader, generated with:
// glslangValidator --target-env vulkan1.2 -V vulkan_as_compaction.rgen -o vulkan_as_compaction.rgen.spirv
// xxd -i vulkan_as_compaction.rgen.spirv > vulkan_as_compaction.rgen.inc
#include "vulkan_as_compaction.rgen.inc"

#include <cstdint>

using Buffer = acceleration_structures::Buffer;
using AccelerationStructure = acceleration_structures::AccelerationStructure;

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

static uint32_t handle_size;
static uint64_t frame_id = 0;

static bool blas_build_frame = false;
static bool tlas_build_frame = false;
static bool keep_source_buffer = false;
static bool buffer_before_source_handles = false;
static bool shared_source = false;
static bool use_second_shared_child = false;
static bool repeat_trim = false;

enum BuildInputDestruction
{
  keep_build_inputs = 0,
  destroy_build_input_memory = 1,
  destroy_build_input_buffers = 2
};
static BuildInputDestruction build_input_destruction;

enum CopyFormat
{
  compact_vkCmdCopyAccelerationStructureKHR = 0,
  clone_vkCmdCopyAccelerationStructureKHR = 1 ,
  copy_vkCmdCopyBuffer = 2 ,
  copy_memcpy = 3
};
static CopyFormat copy_format;

struct Resources
{
	acceleration_structures::functions functions;

	AccelerationStructure blas;
	AccelerationStructure source_alias;
	Buffer blas_buffer;

	AccelerationStructure copied_blas;
	Buffer copied_blas_buffer;
	AccelerationStructure second_child;
	Buffer second_child_buffer;

	AccelerationStructure tlas;
	Buffer tlas_buffer;
	Buffer instance_buffer;
	Buffer result_buffer;
	Buffer blas_scratch;
	Buffer tlas_scratch;

	Buffer vertex_buffer;
	Buffer index_buffer;

	VkDescriptorPool descriptor_pool {VK_NULL_HANDLE};
	VkDescriptorSetLayout descriptor_set_layout { VK_NULL_HANDLE };
	VkDescriptorUpdateTemplate descriptor_update_template { VK_NULL_HANDLE };
	VkDescriptorSet descriptor_set { VK_NULL_HANDLE };

	VkPipelineShaderStageCreateInfo shader_stage;
	VkRayTracingShaderGroupCreateInfoKHR shader_group;
	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };

	Buffer ray_gen_shader_binding_table;

	VkQueue queue{ VK_NULL_HANDLE };
	VkCommandPool command_pool{ VK_NULL_HANDLE};
	VkCommandBuffer command_buffer{ VK_NULL_HANDLE };
	VkQueryPool query_pool{ VK_NULL_HANDLE };
};

static void show_usage()
{
	printf("Test source-BLAS alias destruction after compaction, with checked TLAS ray intersections\n");
	printf("For the lifetime regression use -bbf -tbf and capture frame 3 (after source destruction)\n");
	printf("-tbf/--tlas-build-frame      injects a frame boundary after TLAS is built\n");
	printf("-bbf/--blas-build-frame      injects a frame boundary after BLAS is built\n");
	printf("-ksb/--keep-source-buffer    retain the source BLAS buffer as a control variant\n");
	printf("-bsf/--buffer-before-source-handles destroy source storage before its AS handles\n");
	printf("--shared-source                compact the same source BLAS into two children\n");
	printf("--use-second-shared-child      build the TLAS from the second compacted child\n");
	printf("--destroy-build-input-memory   free BLAS input memory before the trimmed frame\n");
	printf("--destroy-build-input-buffers  destroy BLAS input buffers before freeing their memory\n");
	printf("--repeat-trim                  consume the compacted BLAS again in a later frame\n");
	printf("-cf/--copy-format            ways of copying BLAS (default 0): \n\t0 - vkCmdCopyAccelerationStructureKHR - VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR\n\t1 - vkCmdCopyAccelerationStructureKHR - VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR\n\t2 - vkCmdCopyBuffer\n\t3 - memcpy\n");
}

void prepare_test_resources(const vulkan_setup_t & vulkan, Resources & resources)
{
	resources.functions = acceleration_structures::query_acceleration_structure_functions(vulkan);

	VkCommandPoolCreateInfo command_pool_create_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	command_pool_create_info.queueFamilyIndex = vulkan.queue_family_index;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkResult command_pool_create_result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, nullptr, &resources.command_pool);
	check(command_pool_create_result);

	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &resources.queue);

	VkQueryPoolCreateInfo query_pool_create_info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr };
	query_pool_create_info.queryCount = 1;
	query_pool_create_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
	check(vkCreateQueryPool(vulkan.device, &query_pool_create_info, nullptr, &resources.query_pool));

	resources.vertex_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		vertices.size() * sizeof(Vertex),
		vertices.data(),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);
	resources.vertex_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.vertex_buffer.handle);

	resources.index_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		indices.size() * sizeof(uint32_t),
		indices.data(),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);
	resources.index_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.index_buffer.handle);
}

void free_test_resources(const vulkan_setup_t & vulkan, Resources & resources)
{
	vkDestroyBuffer(vulkan.device, resources.blas_scratch.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.blas_scratch.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.tlas_scratch.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.tlas_scratch.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.result_buffer.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.result_buffer.memory, nullptr);
	if (keep_source_buffer) vkDestroyBuffer(vulkan.device, resources.blas_buffer.handle, nullptr);
	vkDestroyQueryPool(vulkan.device, resources.query_pool, nullptr);
	vkDestroyCommandPool(vulkan.device, resources.command_pool, nullptr);

	vkDestroyPipeline(vulkan.device, resources.pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, resources.pipeline_layout, nullptr);
	vkDestroyShaderModule(vulkan.device, resources.shader_stage.module, nullptr);

	vkDestroyDescriptorUpdateTemplate(vulkan.device, resources.descriptor_update_template, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, resources.descriptor_set_layout, nullptr);
	vkFreeDescriptorSets(vulkan.device, resources.descriptor_pool, 1, &resources.descriptor_set);
	vkDestroyDescriptorPool(vulkan.device, resources.descriptor_pool, nullptr);

	vkFreeMemory(vulkan.device, resources.ray_gen_shader_binding_table.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.ray_gen_shader_binding_table.handle, nullptr);

	resources.functions.vkDestroyAccelerationStructureKHR(vulkan.device, resources.tlas.handle, nullptr);
	resources.functions.vkDestroyAccelerationStructureKHR(vulkan.device, resources.copied_blas.handle, nullptr);
	if (shared_source)
	{
		resources.functions.vkDestroyAccelerationStructureKHR(vulkan.device, resources.second_child.handle, nullptr);
		vkDestroyBuffer(vulkan.device, resources.second_child_buffer.handle, nullptr);
		vkFreeMemory(vulkan.device, resources.second_child_buffer.memory, nullptr);
	}
	vkFreeMemory(vulkan.device, resources.copied_blas_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.copied_blas_buffer.handle, nullptr);

	// The source buffer handle was destroyed after compaction, but its bound
	// memory remains valid until teardown. AS contents can still reside in that
	// allocation after all handles naming the source have gone away.
	vkFreeMemory(vulkan.device, resources.blas_buffer.memory, nullptr);

	vkFreeMemory(vulkan.device, resources.tlas_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.tlas_buffer.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.instance_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.instance_buffer.handle, nullptr);

	if (build_input_destruction == keep_build_inputs)
	{
		vkFreeMemory(vulkan.device, resources.index_buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, resources.index_buffer.handle, nullptr);
		vkFreeMemory(vulkan.device, resources.vertex_buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, resources.vertex_buffer.handle, nullptr);
	}
}

void prepare_acceleration_structures(const vulkan_setup_t & vulkan, Resources & resources)
{
	const uint32_t primitive_count = 1;

	// Build bottom level acceleration structure
	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry.geometry.triangles.vertexData = resources.vertex_buffer.address;
	geometry.geometry.triangles.maxVertex = static_cast<uint32_t>(vertices.size() - 1);
	geometry.geometry.triangles.vertexStride = sizeof(Vertex);
	geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geometry.geometry.triangles.indexData = resources.index_buffer.address;

	VkAccelerationStructureBuildGeometryInfoKHR build_geometry_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
	build_geometry_info.geometryCount = 1;
	build_geometry_info.pGeometries = &geometry;

	VkAccelerationStructureBuildSizesInfoKHR build_size_info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	resources.functions.vkGetAccelerationStructureBuildSizesKHR(
		vulkan.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&build_geometry_info,
		&primitive_count,
		&build_size_info
	);

	VkBufferUsageFlags blas_buffer_usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	if (copy_format == CopyFormat::copy_vkCmdCopyBuffer)
	{
		blas_buffer_usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}

	resources.blas_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		build_size_info.accelerationStructureSize,
		nullptr,
		blas_buffer_usage,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);

	VkAccelerationStructureCreateInfoKHR create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
	create_info.buffer = resources.blas_buffer.handle;
	create_info.size = build_size_info.accelerationStructureSize;
	create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &create_info, nullptr, &resources.blas.handle));

	// A second handle intentionally aliases exactly the same AS storage range.
	check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &create_info, nullptr, &resources.source_alias.handle));

	VkAccelerationStructureDeviceAddressInfoKHR blas_device_adress_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr};

	blas_device_adress_info.accelerationStructure = resources.blas.handle;

	resources.blas.address.deviceAddress = resources.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &blas_device_adress_info);

	blas_device_adress_info.accelerationStructure = resources.source_alias.handle;
	resources.source_alias.address.deviceAddress = resources.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &blas_device_adress_info);

	Buffer scratch_bottom = acceleration_structures::prepare_buffer(
			vulkan,
			build_size_info.buildScratchSize,
			nullptr,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
	scratch_bottom.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, scratch_bottom.handle);
	resources.blas_scratch = scratch_bottom;

	build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_geometry_info.dstAccelerationStructure = resources.blas.handle;
	build_geometry_info.scratchData.deviceAddress = scratch_bottom.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR *build_range_infos;
	VkAccelerationStructureBuildRangeInfoKHR build_range_info;
	build_range_info.primitiveCount = primitive_count;
	build_range_info.primitiveOffset = 0;
	build_range_info.firstVertex = 0;
	build_range_info.transformOffset = 0;
	build_range_infos = &build_range_info;

	VkCommandBufferAllocateInfo command_buffer_allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	command_buffer_allocate_info.commandPool = resources.command_pool;
	command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_allocate_info.commandBufferCount = 1;

	check(vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &resources.command_buffer));
	VkCommandBufferBeginInfo command_buffer_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));
	vkCmdResetQueryPool(resources.command_buffer, resources.query_pool, 0, 1);

	resources.functions.vkCmdBuildAccelerationStructuresKHR(resources.command_buffer, 1, &build_geometry_info, &build_range_infos);

	VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr};
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	vkCmdPipelineBarrier(resources.command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier , 0, nullptr, 0, nullptr);

	resources.functions.vkCmdWriteAccelerationStructuresPropertiesKHR(resources.command_buffer, 1, &resources.blas.handle , VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, resources.query_pool, 0);

	check(vkEndCommandBuffer(resources.command_buffer));
	VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &resources.command_buffer;
	check(vkQueueSubmit(resources.queue, 1, &submitInfo, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(resources.queue));

	// Keep BLAS scratch alive through the trimmed frame; the reconstructed build still references it.

	switch(copy_format)
	{
		case CopyFormat::compact_vkCmdCopyAccelerationStructureKHR :
		{
			VkDeviceSize compacted_size = 0;

			check(vkGetQueryPoolResults(vulkan.device, resources.query_pool, 0, 1, sizeof(VkDeviceSize), &compacted_size, sizeof(VkDeviceSize), VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_64_BIT));

			assert(compacted_size != 0);

			resources.copied_blas_buffer = acceleration_structures::prepare_buffer(
				vulkan,
				compacted_size,
				nullptr,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			VkAccelerationStructureCreateInfoKHR compacted_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
			compacted_create_info.buffer = resources.copied_blas_buffer.handle;
			compacted_create_info.size = compacted_size;
			compacted_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

			check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &compacted_create_info, nullptr, &resources.copied_blas.handle));
			if (shared_source)
			{
				resources.second_child_buffer = acceleration_structures::prepare_buffer(
					vulkan, compacted_size, nullptr,
					VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VkAccelerationStructureCreateInfoKHR second_create_info = compacted_create_info;
				second_create_info.buffer = resources.second_child_buffer.handle;
				check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &second_create_info, nullptr, &resources.second_child.handle));
			}

			VkAccelerationStructureDeviceAddressInfoKHR copied_blas_device_adress_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr};

			copied_blas_device_adress_info.accelerationStructure = resources.copied_blas.handle;

			resources.copied_blas.address.deviceAddress = resources.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &copied_blas_device_adress_info);
			if (shared_source)
			{
				copied_blas_device_adress_info.accelerationStructure = resources.second_child.handle;
				resources.second_child.address.deviceAddress = resources.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &copied_blas_device_adress_info);
			}

			VkCopyAccelerationStructureInfoKHR copy_info{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR, nullptr};
			copy_info.src = resources.blas.handle;
			copy_info.dst = resources.copied_blas.handle;
			copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;

			check(vkResetCommandBuffer(resources.command_buffer, 0));
			check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

			resources.functions.vkCmdCopyAccelerationStructureKHR(resources.command_buffer, &copy_info);
			if (shared_source)
			{
				copy_info.dst = resources.second_child.handle;
				resources.functions.vkCmdCopyAccelerationStructureKHR(resources.command_buffer, &copy_info);
			}

			check(vkEndCommandBuffer(resources.command_buffer));
			check(vkQueueSubmit(resources.queue, 1, &submitInfo, VK_NULL_HANDLE));
			check(vkQueueWaitIdle(resources.queue));
			break;
		}
		case CopyFormat::clone_vkCmdCopyAccelerationStructureKHR :
		{
			resources.copied_blas_buffer = acceleration_structures::prepare_buffer(
				vulkan,
				build_size_info.accelerationStructureSize,
				nullptr,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			VkAccelerationStructureCreateInfoKHR compacted_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
			compacted_create_info.buffer = resources.copied_blas_buffer.handle;
			compacted_create_info.size = build_size_info.accelerationStructureSize;
			compacted_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

			check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &compacted_create_info, nullptr, &resources.copied_blas.handle));

			VkAccelerationStructureDeviceAddressInfoKHR copied_blas_device_adress_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr};

			copied_blas_device_adress_info.accelerationStructure = resources.copied_blas.handle;

			resources.copied_blas.address.deviceAddress = resources.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &copied_blas_device_adress_info);


			VkCopyAccelerationStructureInfoKHR copy_info{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR, nullptr};
			copy_info.src = resources.blas.handle;
			copy_info.dst = resources.copied_blas.handle;
			copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;

			check(vkResetCommandBuffer(resources.command_buffer, 0));
			check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

			resources.functions.vkCmdCopyAccelerationStructureKHR(resources.command_buffer, &copy_info);

			check(vkEndCommandBuffer(resources.command_buffer));
			check(vkQueueSubmit(resources.queue, 1, &submitInfo, VK_NULL_HANDLE));
			check(vkQueueWaitIdle(resources.queue));
			break;
		}
		case CopyFormat::copy_vkCmdCopyBuffer :
		{
			resources.copied_blas_buffer = acceleration_structures::prepare_buffer(
				vulkan,
				build_size_info.accelerationStructureSize,
				nullptr,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			VkAccelerationStructureCreateInfoKHR compacted_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
			compacted_create_info.buffer = resources.copied_blas_buffer.handle;
			compacted_create_info.size = build_size_info.accelerationStructureSize;
			compacted_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

			check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &compacted_create_info, nullptr, &resources.copied_blas.handle));

			VkAccelerationStructureDeviceAddressInfoKHR copied_blas_device_adress_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr};

			copied_blas_device_adress_info.accelerationStructure = resources.copied_blas.handle;

			resources.copied_blas.address.deviceAddress = resources.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &copied_blas_device_adress_info);


			const VkBufferCopy info_regions{0,0, build_size_info.accelerationStructureSize};

			check(vkResetCommandBuffer(resources.command_buffer, 0));
			check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

			vkCmdCopyBuffer(resources.command_buffer, resources.blas_buffer.handle,  resources.copied_blas_buffer.handle, 1, &info_regions);

			check(vkEndCommandBuffer(resources.command_buffer));
			check(vkQueueSubmit(resources.queue, 1, &submitInfo, VK_NULL_HANDLE));
			check(vkQueueWaitIdle(resources.queue));
			break;
		}
		case CopyFormat::copy_memcpy :
		{
			resources.copied_blas_buffer = acceleration_structures::prepare_buffer(
				vulkan,
				build_size_info.accelerationStructureSize,
				nullptr,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			VkAccelerationStructureCreateInfoKHR compacted_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
			compacted_create_info.buffer = resources.copied_blas_buffer.handle;
			compacted_create_info.size = build_size_info.accelerationStructureSize;
			compacted_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

			check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &compacted_create_info, nullptr, &resources.copied_blas.handle));

			VkAccelerationStructureDeviceAddressInfoKHR copied_blas_device_adress_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr};

			copied_blas_device_adress_info.accelerationStructure = resources.copied_blas.handle;

			resources.copied_blas.address.deviceAddress = resources.functions.vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &copied_blas_device_adress_info);

			void* data_original_blas;
			void* data_copy_blas;

			check(vkMapMemory(vulkan.device, resources.blas_buffer.memory, 0, build_size_info.accelerationStructureSize, 0, &data_original_blas));
			check(vkMapMemory(vulkan.device, resources.copied_blas_buffer.memory, 0, build_size_info.accelerationStructureSize, 0, &data_copy_blas));

			memcpy(data_copy_blas, data_original_blas, build_size_info.accelerationStructureSize);  // Adjust the size if necessary

			if (vulkan.has_explicit_host_updates) testFlushMemory(vulkan, resources.copied_blas_buffer.memory, 0, build_size_info.accelerationStructureSize, true);
			vkUnmapMemory(vulkan.device, resources.blas_buffer.memory);
			vkUnmapMemory(vulkan.device, resources.copied_blas_buffer.memory);
			break;
		}
		default:
			printf("Wrong copy format provided. Abort.");
			fflush(stdout);
			exit(-1);
	}


	// Frame 1 ends after compaction, while both source handles still exist.
	if (blas_build_frame){
		VkFrameBoundaryEXT frameBoundary = {};
		frameBoundary.sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT;
		frameBoundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
		frameBoundary.pNext = nullptr;
		frameBoundary.frameID = frame_id++;

		VkSubmitInfo submitInfo2 = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
		submitInfo2.pNext = &frameBoundary;
		submitInfo2.commandBufferCount = 0;
		submitInfo2.pCommandBuffers = nullptr;

		check(vkQueueSubmit(resources.queue, 1, &submitInfo2, VK_NULL_HANDLE));
		check(vkQueueWaitIdle(resources.queue));
	}

	static VkTransformMatrixKHR identity = {{
			{1.0f, 0.0f, 0.0f, 0.0f},
			{0.0f, 1.0f, 0.0f, 0.0f},
			{0.0f, 0.0f, 1.0f, 0.0f}
	}};

	// Build top level acceleration structure
	VkAccelerationStructureInstanceKHR instance;
	instance.transform = identity;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = 0;
	instance.accelerationStructureReference = use_second_shared_child ?
		resources.second_child.address.deviceAddress : resources.copied_blas.address.deviceAddress;
	VkMarkingTypeARM instance_marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
	VkMarkingSubTypeARM instance_sub_type{};
	instance_sub_type.deviceAddressType = VK_DEVICE_ADDRESS_TYPE_ACCELERATION_STRUCTURE_ARM;
	VkDeviceSize instance_marked_offset = offsetof(VkAccelerationStructureInstanceKHR, accelerationStructureReference);
	VkMarkedOffsetsARM instance_markings{VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr};
	instance_markings.count = 1;
	instance_markings.pMarkingTypes = &instance_marking_type;
	instance_markings.pSubTypes = &instance_sub_type;
	instance_markings.pOffsets = &instance_marked_offset;

	resources.instance_buffer = acceleration_structures::prepare_buffer(
			vulkan,
			sizeof(VkAccelerationStructureInstanceKHR),
			&instance,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			vulkan.has_trace_helpers ? &instance_markings : nullptr
		);

	resources.instance_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.instance_buffer.handle);

	VkAccelerationStructureGeometryInstancesDataKHR instance_data;
	instance_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instance_data.pNext = nullptr;
	instance_data.arrayOfPointers = VK_FALSE;
	instance_data.data = resources.instance_buffer.address;

	VkAccelerationStructureBuildRangeInfoKHR as_range_info{};
	as_range_info.primitiveOffset = 0;
	as_range_info.primitiveCount = 1;
	as_range_info.firstVertex = 0;
	as_range_info.transformOffset = 0;

	VkAccelerationStructureGeometryKHR as_geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	as_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	as_geometry.geometry.instances = instance_data;

	VkAccelerationStructureBuildGeometryInfoKHR as_build_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	as_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
	as_build_info.geometryCount = 1;
	as_build_info.pGeometries = &as_geometry;
	as_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	as_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	as_build_info.srcAccelerationStructure = VK_NULL_HANDLE;

	VkAccelerationStructureBuildSizesInfoKHR as_build_size_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	resources.functions.vkGetAccelerationStructureBuildSizesKHR(
		vulkan.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_OR_DEVICE_KHR,
		&as_build_info,
		&as_range_info.primitiveCount,
		&as_build_size_info
	);

	resources.tlas_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		as_build_size_info.accelerationStructureSize,
		nullptr,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	);

	VkAccelerationStructureCreateInfoKHR as_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
	as_create_info.type = as_build_info.type;
	as_create_info.size = as_build_size_info.accelerationStructureSize;
	as_create_info.buffer = resources.tlas_buffer.handle;
	as_create_info.offset = 0;

	check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &as_create_info, nullptr, &resources.tlas.handle));

	as_build_info.dstAccelerationStructure = resources.tlas.handle;

	Buffer scratch_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		as_build_size_info.buildScratchSize,
		nullptr,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	);
	scratch_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, scratch_buffer.handle);
	resources.tlas_scratch = scratch_buffer;
	as_build_info.scratchData.deviceAddress = scratch_buffer.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR * as_range_infos = &as_range_info;

	check(vkResetCommandBuffer(resources.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

	resources.functions.vkCmdBuildAccelerationStructuresKHR(resources.command_buffer, 1, &as_build_info, &as_range_infos);

	check(vkEndCommandBuffer(resources.command_buffer));
	check(vkQueueSubmit(resources.queue, 1, &submitInfo, VK_NULL_HANDLE));

	check(vkQueueWaitIdle(resources.queue));

	// Keep TLAS scratch alive through the trimmed frame; the reconstructed build still references it.

	// Both destruction orders are valid after the source work has completed. The
	// buffer-first variant also occurs during gfxrecon-replay recapture cleanup.
	// Its bound memory intentionally remains live in either case.
	if (!keep_source_buffer && buffer_before_source_handles)
	{
		vkDestroyBuffer(vulkan.device, resources.blas_buffer.handle, nullptr);
	}
	resources.functions.vkDestroyAccelerationStructureKHR(vulkan.device, resources.source_alias.handle, nullptr);
	resources.functions.vkDestroyAccelerationStructureKHR(vulkan.device, resources.blas.handle, nullptr);
	if (!keep_source_buffer && !buffer_before_source_handles)
	{
		vkDestroyBuffer(vulkan.device, resources.blas_buffer.handle, nullptr);
	}

	if (build_input_destruction == destroy_build_input_memory)
	{
		vkFreeMemory(vulkan.device, resources.index_buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, resources.index_buffer.handle, nullptr);
		vkFreeMemory(vulkan.device, resources.vertex_buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, resources.vertex_buffer.handle, nullptr);
	}
	else if (build_input_destruction == destroy_build_input_buffers)
	{
		vkDestroyBuffer(vulkan.device, resources.index_buffer.handle, nullptr);
		vkFreeMemory(vulkan.device, resources.index_buffer.memory, nullptr);
		vkDestroyBuffer(vulkan.device, resources.vertex_buffer.handle, nullptr);
		vkFreeMemory(vulkan.device, resources.vertex_buffer.memory, nullptr);
	}

	if (tlas_build_frame)
	{
		VkFrameBoundaryEXT frameBoundary = {};
		frameBoundary.sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT;
		frameBoundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
		frameBoundary.pNext = nullptr;
		frameBoundary.frameID = frame_id++;

		VkSubmitInfo submitInfo2 = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
		submitInfo2.pNext = &frameBoundary;
		submitInfo2.commandBufferCount = 0;
		submitInfo2.pCommandBuffers = nullptr;

		check(vkQueueSubmit(resources.queue, 1, &submitInfo2, VK_NULL_HANDLE));
		check(vkQueueWaitIdle(resources.queue));
	}

}

void prepare_ray_tracing_pipeline(const vulkan_setup_t & vulkan, Resources & resources)
{
	VkDescriptorSetLayoutBinding layout_binding{};
	layout_binding.binding         = 0;
	layout_binding.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	layout_binding.descriptorCount = 1;
	layout_binding.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	VkDescriptorSetLayoutBinding bindings[2] = {layout_binding, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr}};
	layout_info.bindingCount = 2;
	layout_info.pBindings    = bindings;
	check(vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &resources.descriptor_set_layout));

	VkPipelineLayoutCreateInfo pipeline_layout_create_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts    = &resources.descriptor_set_layout;
	check(vkCreatePipelineLayout(vulkan.device, &pipeline_layout_create_info, nullptr, &resources.pipeline_layout));

	resources.shader_stage = acceleration_structures::prepare_shader_stage_create_info(vulkan, vulkan_as_compaction_rgen_spirv, vulkan_as_compaction_rgen_spirv_len, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
	resources.shader_group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	resources.shader_group.pNext = nullptr;
	resources.shader_group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	resources.shader_group.generalShader = 0;
	resources.shader_group.closestHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_group.anyHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_group.intersectionShader = VK_SHADER_UNUSED_KHR;

	VkRayTracingPipelineCreateInfoKHR pipeline_create_info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR, nullptr};
	pipeline_create_info.stageCount = 1;
	pipeline_create_info.pStages = &resources.shader_stage;
	pipeline_create_info.groupCount = 1;
	pipeline_create_info.pGroups = &resources.shader_group;
	pipeline_create_info.maxPipelineRayRecursionDepth = 1;
	pipeline_create_info.layout = resources.pipeline_layout;

	check(resources.functions.vkCreateRayTracingPipelinesKHR(vulkan.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &resources.pipeline));
}

void prepare_shader_binding_table(const vulkan_setup_t & vulkan, Resources & resources)
{
	const uint32_t group_count = 1;
	const uint32_t sbt_size = group_count * handle_size;

	std::vector<uint8_t> shader_handle_storage(sbt_size);
	check(resources.functions.vkGetRayTracingShaderGroupHandlesKHR(vulkan.device, resources.pipeline, 0, group_count, sbt_size, shader_handle_storage.data()));

	const VkBufferUsageFlags buffer_usage_flags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	const VkMemoryPropertyFlags memory_usage_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	resources.ray_gen_shader_binding_table = acceleration_structures::prepare_buffer(vulkan, handle_size, nullptr, buffer_usage_flags, memory_usage_flags);

	void * mapped = nullptr;
	vkMapMemory(vulkan.device, resources.ray_gen_shader_binding_table.memory, 0, handle_size, 0, &mapped);
	memcpy(mapped, shader_handle_storage.data(), handle_size);
	testFlushMemoryShaderGroupHandles(
		vulkan,
		resources.ray_gen_shader_binding_table.memory,
		0,
		handle_size,
		{0},
		{VK_SHADER_GROUP_SHADER_GENERAL_KHR});
	vkUnmapMemory(vulkan.device, resources.ray_gen_shader_binding_table.memory);
}

void prepare_descriptor_set(const vulkan_setup_t &vulkan, Resources & resources)
{
	VkDescriptorPoolSize as_pool_size;
	as_pool_size.type = VkDescriptorType::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	as_pool_size.descriptorCount = 1;

	VkDescriptorPoolCreateInfo descriptor_pool_create_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr};
	descriptor_pool_create_info.flags = VkDescriptorPoolCreateFlagBits::VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	descriptor_pool_create_info.maxSets = 1;
	VkDescriptorPoolSize pool_sizes[2] = {as_pool_size, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
	descriptor_pool_create_info.poolSizeCount = 2;
	descriptor_pool_create_info.pPoolSizes = pool_sizes;

	check(vkCreateDescriptorPool(vulkan.device, &descriptor_pool_create_info, nullptr, &resources.descriptor_pool));

	VkDescriptorSetAllocateInfo allocate_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr};
	allocate_info.descriptorPool = resources.descriptor_pool;
	allocate_info.descriptorSetCount = 1;
	allocate_info.pSetLayouts = &resources.descriptor_set_layout;

	check(vkAllocateDescriptorSets(vulkan.device, &allocate_info, &resources.descriptor_set));
}

void bind_and_trace(const vulkan_setup_t & vulkan, Resources & resources)
{
	uint32_t initial[2] = {0xdeadbeef, 0xdeadbeef};
	resources.result_buffer = acceleration_structures::prepare_buffer(vulkan, sizeof(initial), initial,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDescriptorBufferInfo result_info{resources.result_buffer.handle, 0, sizeof(initial)};
	VkWriteDescriptorSet result_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	result_write.dstSet = resources.descriptor_set;
	result_write.dstBinding = 1;
	result_write.descriptorCount = 1;
	result_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	result_write.pBufferInfo = &result_info;
	vkUpdateDescriptorSets(vulkan.device, 1, &result_write, 0, nullptr);
	VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo{};
	accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	accelerationStructureInfo.accelerationStructureCount = 1;
	accelerationStructureInfo.pAccelerationStructures = &resources.tlas.handle;

	VkWriteDescriptorSet writeDescriptorSet{};
	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.pNext = &accelerationStructureInfo;
	writeDescriptorSet.dstSet = resources.descriptor_set;
	writeDescriptorSet.dstBinding = 0;
	writeDescriptorSet.dstArrayElement = 0;
	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	vkUpdateDescriptorSets(vulkan.device, 1, &writeDescriptorSet, 0, nullptr);

	VkStridedDeviceAddressRegionKHR ray_get_sbt_entry{};
	ray_get_sbt_entry.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.ray_gen_shader_binding_table.handle);
	ray_get_sbt_entry.stride = handle_size;
	ray_get_sbt_entry.size = handle_size;

	VkStridedDeviceAddressRegionKHR ray_miss_shader_sbt_entry{};
	VkStridedDeviceAddressRegionKHR ray_hit_sbt_entry{};
	VkStridedDeviceAddressRegionKHR callable_sbt_entry{};

	VkCommandBufferBeginInfo command_buffer_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(resources.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

	vkCmdBindPipeline(resources.command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, resources.pipeline);
	vkCmdBindDescriptorSets(resources.command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, resources.pipeline_layout, 0, 1, &resources.descriptor_set, 0, 0);

	resources.functions.vkCmdTraceRaysKHR(
				resources.command_buffer,
				&ray_get_sbt_entry,
				&ray_miss_shader_sbt_entry,
				&ray_hit_sbt_entry,
				&callable_sbt_entry,
				1,
				1,
				1);

	VkMemoryBarrier readback{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
	vkCmdPipelineBarrier(resources.command_buffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &readback, 0, nullptr, 0, nullptr);
	check(vkEndCommandBuffer(resources.command_buffer));

	VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &resources.command_buffer;

	check(vkQueueSubmit(resources.queue, 1, &submitInfo, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(resources.queue));
	// Check before mapping: page-guard capture can emit a FillMemoryCommand for
	// a read-only host mapping. Replaying that upload before the checksum would
	// overwrite the GPU result with the captured result and mask a bad replay.
	if (vulkan.vkAssertBuffer && !getenv("TOOLSTEST_NULL_RUN"))
	{
		uint32_t crc = 0;
		const VkUpdateBufferInfoARM info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr,
			resources.result_buffer.handle, 0, sizeof(initial), nullptr};
		check(vulkan.vkAssertBuffer(vulkan.device, &info, &crc, "compacted BLAS hit/miss"));
	}
	void* mapped = nullptr;
	check(vkMapMemory(vulkan.device, resources.result_buffer.memory, 0, sizeof(initial), 0, &mapped));
	auto* hits = static_cast<const uint32_t*>(mapped);
	if (!getenv("TOOLSTEST_NULL_RUN"))
	{
		assert(hits[0] == 1 && hits[1] == 0);
		printf("AS intersection results: hit=%u miss=%u\n", hits[0], hits[1]);
	}
	vkUnmapMemory(vulkan.device, resources.result_buffer.memory);
}

bool parse_parameters(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-tbf", "--tlas-build-frame"))
	{
		reqs.options["tlas-build-frame"] = true;
		return true;
	}
	else if (match(argv[i], "-bbf", "--blas-build-frame"))
	{
		reqs.options["blas-build-frame"] = true;
		return true;
	}
	else if (match(argv[i], "-ksb", "--keep-source-buffer"))
	{
		reqs.options["keep-source-buffer"] = true;
		return true;
	}
	else if (match(argv[i], "-bsf", "--buffer-before-source-handles"))
	{
		reqs.options["buffer-before-source-handles"] = true;
		return true;
	}
	else if (!strcmp(argv[i], "--shared-source"))
	{
		reqs.options["shared-source"] = true;
		return true;
	}
	else if (!strcmp(argv[i], "--use-second-shared-child"))
	{
		reqs.options["use-second-shared-child"] = true;
		return true;
	}
	else if (!strcmp(argv[i], "--destroy-build-input-memory"))
	{
		reqs.options["build-input-destruction"] = destroy_build_input_memory;
		return true;
	}
	else if (!strcmp(argv[i], "--destroy-build-input-buffers"))
	{
		reqs.options["build-input-destruction"] = destroy_build_input_buffers;
		return true;
	}
	else if (!strcmp(argv[i], "--repeat-trim"))
	{
		reqs.options["repeat-trim"] = true;
		return true;
	}
	else if (match(argv[i], "-cf", "--copy-format"))
	{
		int copy_format_variant = get_arg(argv, ++i, argc);

		if (copy_format_variant == 0) copy_format = CopyFormat::compact_vkCmdCopyAccelerationStructureKHR;
		else if (copy_format_variant == 1)  copy_format = CopyFormat::clone_vkCmdCopyAccelerationStructureKHR;
		else if (copy_format_variant == 2)  copy_format = CopyFormat::copy_vkCmdCopyBuffer;
		else if (copy_format_variant == 3)  copy_format = CopyFormat::copy_memcpy;
		else assert(0);

		return true;
	}
	return false;
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return parse_parameters(i, argc, argv, reqs);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.options["tlas-build-frame"] = false;
	reqs.options["blas-build-frame"] = false;
	reqs.options["keep-source-buffer"] = false;
	reqs.options["buffer-before-source-handles"] = false;
	reqs.options["copy-format"] = 0;
	reqs.options["shared-source"] = false;
	reqs.options["use-second-shared-child"] = false;
	reqs.options["build-input-destruction"] = keep_build_inputs;
	reqs.options["repeat-trim"] = false;

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr, VK_TRUE};
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr, VK_TRUE};
	ray_tracing_features.pNext = &ray_query_features;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accfeats = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray_tracing_features, VK_TRUE };
	reqs.device_extensions.push_back("VK_KHR_acceleration_structure");
	reqs.device_extensions.push_back("VK_KHR_deferred_host_operations");
	reqs.device_extensions.push_back("VK_KHR_ray_tracing_pipeline");
	reqs.device_extensions.push_back("VK_KHR_ray_query");
	reqs.bufferDeviceAddress = true;
	reqs.extension_features = (VkBaseInStructure*)&accfeats;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.queues = 1;
	reqs.required_queue_flags = VK_QUEUE_COMPUTE_BIT;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_as_compaction_lifetime", reqs);

	tlas_build_frame = std::get<bool>(reqs.options.at("tlas-build-frame"));
	blas_build_frame = std::get<bool>(reqs.options.at("blas-build-frame"));
	keep_source_buffer = std::get<bool>(reqs.options.at("keep-source-buffer"));
	buffer_before_source_handles = std::get<bool>(reqs.options.at("buffer-before-source-handles"));
	shared_source = std::get<bool>(reqs.options.at("shared-source"));
	use_second_shared_child = std::get<bool>(reqs.options.at("use-second-shared-child"));
	build_input_destruction = static_cast<BuildInputDestruction>(std::get<int>(reqs.options.at("build-input-destruction")));
	repeat_trim = std::get<bool>(reqs.options.at("repeat-trim"));
	assert(!keep_source_buffer || !buffer_before_source_handles);
	assert(!use_second_shared_child || shared_source);
	assert(!shared_source || copy_format == compact_vkCmdCopyAccelerationStructureKHR);

	handle_size = vulkan.device_ray_tracing_pipeline_properties.shaderGroupHandleSize;

	Resources resources{};

	prepare_test_resources(vulkan, resources);
	prepare_acceleration_structures(vulkan, resources);
	prepare_ray_tracing_pipeline(vulkan, resources);
	prepare_shader_binding_table(vulkan, resources);
	prepare_descriptor_set(vulkan, resources);

	bind_and_trace(vulkan, resources);


	// Close the captured frame range after the compacted AS has been consumed.
	// With -bbf -tbf, the preceding TLAS boundary starts frame 3. This boundary
	// finalizes its trace after both the GPU work and the replayable checksum.
	if (tlas_build_frame)
	{
		VkFrameBoundaryEXT frameBoundary = {};
		frameBoundary.sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT;
		frameBoundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
		frameBoundary.frameID = frame_id++;

		VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
		submitInfo.pNext = &frameBoundary;
		check(vkQueueSubmit(resources.queue, 1, &submitInfo, VK_NULL_HANDLE));
		check(vkQueueWaitIdle(resources.queue));
	}

	if (repeat_trim)
	{
		if (tlas_build_frame)
		{
			VkFrameBoundaryEXT frame_boundary{};
			frame_boundary.sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT;
			frame_boundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
			frame_boundary.frameID = frame_id++;
			VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, &frame_boundary};
			check(vkQueueSubmit(resources.queue, 1, &submit_info, VK_NULL_HANDLE));
			check(vkQueueWaitIdle(resources.queue));
		}
		vkDestroyBuffer(vulkan.device, resources.result_buffer.handle, nullptr);
		vkFreeMemory(vulkan.device, resources.result_buffer.memory, nullptr);
		bind_and_trace(vulkan, resources);
		if (tlas_build_frame)
		{
			VkFrameBoundaryEXT frame_boundary{};
			frame_boundary.sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT;
			frame_boundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
			frame_boundary.frameID = frame_id++;
			VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, &frame_boundary};
			check(vkQueueSubmit(resources.queue, 1, &submit_info, VK_NULL_HANDLE));
			check(vkQueueWaitIdle(resources.queue));
		}
	}
	free_test_resources(vulkan, resources);

	test_done(vulkan);
	return 0;
}
