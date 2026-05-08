#include "vulkan_common.h"
#include "vulkan_raytracing_common.h"

#include "vulkan_raytracing_indirect_noop.rgen.inc"

struct Resources
{
	ray_tracing_common::Context context;

	VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
	VkPipeline pipeline{VK_NULL_HANDLE};
	VkPipelineShaderStageCreateInfo shader_stage{};
	VkRayTracingShaderGroupCreateInfoKHR shader_group{};

	acceleration_structures::Buffer sbt_buffer;
	VkDeviceAddress sbt_address{0};
	VkDeviceSize sbt_size{0};

	acceleration_structures::Buffer indirect_buffer;
	VkDeviceAddress indirect_address{0};
};

static void show_usage()
{
	printf("Minimal vkCmdTraceRaysIndirect2KHR no-op test using one raygen SBT record\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)i;
	(void)argc;
	(void)argv;
	(void)reqs;
	return false;
}

static void create_pipeline(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	check(vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &resources.pipeline_layout));
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)resources.pipeline_layout, "raytracing_indirect_noop_pipeline_layout");

	resources.shader_stage = acceleration_structures::prepare_shader_stage_create_info(
		vulkan,
		vulkan_raytracing_indirect_noop_raygen_spv,
		vulkan_raytracing_indirect_noop_raygen_spv_len,
		VK_SHADER_STAGE_RAYGEN_BIT_KHR);

	resources.shader_group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	resources.shader_group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	resources.shader_group.generalShader = 0;
	resources.shader_group.closestHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_group.anyHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_group.intersectionShader = VK_SHADER_UNUSED_KHR;

	VkRayTracingPipelineCreateInfoKHR pipeline_info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR, nullptr};
	pipeline_info.stageCount = 1;
	pipeline_info.pStages = &resources.shader_stage;
	pipeline_info.groupCount = 1;
	pipeline_info.pGroups = &resources.shader_group;
	pipeline_info.maxPipelineRayRecursionDepth = 1;
	pipeline_info.layout = resources.pipeline_layout;

	check(resources.context.functions.vkCreateRayTracingPipelinesKHR(
		vulkan.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &resources.pipeline));
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)resources.pipeline, "raytracing_indirect_noop_pipeline");
}

static void create_sbt(const vulkan_setup_t& vulkan, Resources& resources)
{
	const uint32_t handle_size = vulkan.device_ray_tracing_pipeline_properties.shaderGroupHandleSize;
	const uint32_t handle_alignment = vulkan.device_ray_tracing_pipeline_properties.shaderGroupHandleAlignment;
	const uint32_t entry_size = static_cast<uint32_t>(aligned_size(handle_size, handle_alignment));

	std::vector<uint8_t> handle_storage(handle_size);
	check(resources.context.functions.vkGetRayTracingShaderGroupHandlesKHR(
		vulkan.device, resources.pipeline, 0, 1, handle_storage.size(), handle_storage.data()));

	resources.sbt_size = entry_size;
	resources.sbt_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		resources.sbt_size,
		nullptr,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.sbt_buffer.handle, "raytracing_indirect_noop_sbt");

	uint8_t* mapped = nullptr;
	check(vkMapMemory(vulkan.device, resources.sbt_buffer.memory, 0, resources.sbt_size, 0, reinterpret_cast<void**>(&mapped)));
	memcpy(mapped, handle_storage.data(), handle_size);
	testFlushMemoryShaderGroupHandles(
		vulkan,
		resources.sbt_buffer.memory,
		0,
		resources.sbt_size,
		{0},
		{VK_SHADER_GROUP_SHADER_GENERAL_KHR});
	vkUnmapMemory(vulkan.device, resources.sbt_buffer.memory);

	resources.sbt_address = acceleration_structures::get_buffer_device_address(vulkan, resources.sbt_buffer.handle);
	assert((resources.sbt_address & (vulkan.device_ray_tracing_pipeline_properties.shaderGroupBaseAlignment - 1)) == 0);
}

static void create_indirect_buffer(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkTraceRaysIndirectCommand2KHR indirect_command{};
	indirect_command.raygenShaderRecordAddress = resources.sbt_address;
	indirect_command.raygenShaderRecordSize = resources.sbt_size;
	indirect_command.width = 1;
	indirect_command.height = 1;
	indirect_command.depth = 1;

	VkMarkingTypeARM marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
	VkMarkingSubTypeARM sub_type{};
	sub_type.deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
	VkDeviceSize marked_offset = offsetof(VkTraceRaysIndirectCommand2KHR, raygenShaderRecordAddress);
	VkMarkedOffsetsARM markings{VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr};
	markings.count = 1;
	markings.pMarkingTypes = &marking_type;
	markings.pSubTypes = &sub_type;
	markings.pOffsets = &marked_offset;

	resources.indirect_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(indirect_command),
		&indirect_command,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vulkan.has_trace_helpers ? &markings : nullptr);
	resources.indirect_address = acceleration_structures::get_buffer_device_address(vulkan, resources.indirect_buffer.handle);
	assert((resources.indirect_address & 0x3u) == 0 && "TraceRaysIndirect2 address must be 4-byte aligned");
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.indirect_buffer.handle, "raytracing_indirect_noop_indirect");
}

static void trace(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(resources.context.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.context.command_buffer, &begin_info));

	vkCmdBindPipeline(resources.context.command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, resources.pipeline);
	test_marker_mention(vulkan, "Tracing one no-op raygen shader indirectly", VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.indirect_buffer.handle);

	resources.context.functions.vkCmdTraceRaysIndirect2KHR(resources.context.command_buffer, resources.indirect_address);

	check(vkEndCommandBuffer(resources.context.command_buffer));

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &resources.context.command_buffer;
	check(vkQueueSubmit(resources.context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(resources.context.queue));
}

static void cleanup(const vulkan_setup_t& vulkan, Resources& resources)
{
	vkDestroyPipeline(vulkan.device, resources.pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, resources.pipeline_layout, nullptr);

	if (resources.shader_stage.module != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(vulkan.device, resources.shader_stage.module, nullptr);
	}

	vkFreeMemory(vulkan.device, resources.sbt_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.sbt_buffer.handle, nullptr);

	vkFreeMemory(vulkan.device, resources.indirect_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.indirect_buffer.handle, nullptr);

	ray_tracing_common::destroy_context(vulkan, resources.context);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray_tracing_features};
	VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR maintenance1_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, &acceleration_features};

	maintenance1_features.rayTracingMaintenance1 = VK_TRUE;
	maintenance1_features.rayTracingPipelineTraceRaysIndirect2 = VK_TRUE;
	ray_tracing_features.rayTracingPipeline = VK_TRUE;
	acceleration_features.accelerationStructure = VK_TRUE;

	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.descriptorIndexing = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&maintenance1_features);
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.queues = 1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_raytracing_indirect_noop", reqs);
	assert(vulkan.hasfeat12.bufferDeviceAddress && "Buffer device address required");

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rt_supported};
	VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR maintenance1_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, &as_supported};
	VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &maintenance1_supported};
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &supported);
	assert(rt_supported.rayTracingPipeline && "Ray tracing pipeline feature required");
	assert(as_supported.accelerationStructure && "Acceleration structure feature required");
	assert(maintenance1_supported.rayTracingMaintenance1 && "Ray tracing maintenance1 feature required");
	assert(maintenance1_supported.rayTracingPipelineTraceRaysIndirect2 && "TraceRaysIndirect2 feature required");

	Resources resources{};
	ray_tracing_common::init_context(vulkan, resources.context);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)resources.context.command_pool, "raytracing_indirect_noop_command_pool");
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)resources.context.command_buffer, "raytracing_indirect_noop_command_buffer");

	bench_start_iteration(vulkan.bench);
	create_pipeline(vulkan, resources);
	create_sbt(vulkan, resources);
	create_indirect_buffer(vulkan, resources);
	trace(vulkan, resources);
	bench_stop_iteration(vulkan.bench);

	cleanup(vulkan, resources);
	test_done(vulkan);
	return 0;
}
