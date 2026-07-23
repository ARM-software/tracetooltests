#include "vulkan_common.h"
#include "vulkan_raytracing_common.h"

#include "vulkan_raytracing_callable_bda.rgen.inc"
#include "vulkan_raytracing_callable_bda.rcall.inc"

static constexpr uint32_t kWrittenValue = 0xc011ab1e;

struct PushConstants
{
	VkDeviceAddress target_address;
	VkDeviceAddress decoy_address;
};

struct Resources
{
	ray_tracing_common::Context context;

	acceleration_structures::Buffer target_buffer;
	acceleration_structures::Buffer decoy_buffer;
	acceleration_structures::Buffer sbt_buffer;

	VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
	VkPipeline pipeline{VK_NULL_HANDLE};
	VkPipelineShaderStageCreateInfo shader_stages[2]{};
	VkRayTracingShaderGroupCreateInfoKHR shader_groups[2]{};

	VkStridedDeviceAddressRegionKHR raygen_region{};
	VkStridedDeviceAddressRegionKHR callable_region{};
};

static void show_usage()
{
	printf("Pass a buffer address back from a callable ray tracing shader\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)i;
	(void)argc;
	(void)argv;
	(void)reqs;
	return false;
}

static void create_output_buffers(const vulkan_setup_t& vulkan, Resources& resources)
{
	const uint32_t initial_value = 0;
	const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	const VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	resources.target_buffer = acceleration_structures::prepare_buffer(
		vulkan, sizeof(initial_value), &initial_value, usage, memory_properties);
	resources.decoy_buffer = acceleration_structures::prepare_buffer(
		vulkan, sizeof(initial_value), &initial_value, usage, memory_properties);

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.target_buffer.handle, "callable_bda_target");
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.decoy_buffer.handle, "callable_bda_decoy");
}

static void create_pipeline(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkPushConstantRange push_constant_range{};
	push_constant_range.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR;
	push_constant_range.offset = 0;
	push_constant_range.size = sizeof(PushConstants);

	VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges = &push_constant_range;
	VkResult result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &resources.pipeline_layout);
	check(result);

	resources.shader_stages[0] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan,
		vulkan_raytracing_callable_bda_raygen_spv,
		vulkan_raytracing_callable_bda_raygen_spv_len,
		VK_SHADER_STAGE_RAYGEN_BIT_KHR);
	resources.shader_stages[1] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan,
		vulkan_raytracing_callable_bda_callable_spv,
		vulkan_raytracing_callable_bda_callable_spv_len,
		VK_SHADER_STAGE_CALLABLE_BIT_KHR);

	for (uint32_t i = 0; i < 2; ++i)
	{
		resources.shader_groups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		resources.shader_groups[i].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		resources.shader_groups[i].generalShader = i;
		resources.shader_groups[i].closestHitShader = VK_SHADER_UNUSED_KHR;
		resources.shader_groups[i].anyHitShader = VK_SHADER_UNUSED_KHR;
		resources.shader_groups[i].intersectionShader = VK_SHADER_UNUSED_KHR;
	}

	VkRayTracingPipelineCreateInfoKHR pipeline_info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR, nullptr};
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = resources.shader_stages;
	pipeline_info.groupCount = 2;
	pipeline_info.pGroups = resources.shader_groups;
	pipeline_info.maxPipelineRayRecursionDepth = 1;
	pipeline_info.layout = resources.pipeline_layout;

	result = resources.context.functions.vkCreateRayTracingPipelinesKHR(
		vulkan.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &resources.pipeline);
	check(result);
}

static void create_sbt(const vulkan_setup_t& vulkan, Resources& resources)
{
	const uint32_t handle_size = vulkan.device_ray_tracing_pipeline_properties.shaderGroupHandleSize;
	const uint32_t handle_alignment = vulkan.device_ray_tracing_pipeline_properties.shaderGroupHandleAlignment;
	const uint32_t base_alignment = vulkan.device_ray_tracing_pipeline_properties.shaderGroupBaseAlignment;
	const uint32_t entry_size = static_cast<uint32_t>(aligned_size(handle_size, handle_alignment));
	const uint32_t callable_offset = static_cast<uint32_t>(aligned_size(entry_size, base_alignment));
	const uint32_t sbt_size = callable_offset + entry_size;

	std::vector<uint8_t> handles(2 * handle_size);
	VkResult result = resources.context.functions.vkGetRayTracingShaderGroupHandlesKHR(
		vulkan.device, resources.pipeline, 0, 2, handles.size(), handles.data());
	check(result);

	resources.sbt_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sbt_size,
		nullptr,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	uint8_t* mapped = nullptr;
	result = vkMapMemory(vulkan.device, resources.sbt_buffer.memory, 0, sbt_size, 0, reinterpret_cast<void**>(&mapped));
	check(result);
	memcpy(mapped, handles.data(), handle_size);
	memcpy(mapped + callable_offset, handles.data() + handle_size, handle_size);
	testFlushMemoryShaderGroupHandles(
		vulkan,
		resources.sbt_buffer.memory,
		0,
		sbt_size,
		{0, callable_offset},
		{VK_SHADER_GROUP_SHADER_GENERAL_KHR, VK_SHADER_GROUP_SHADER_GENERAL_KHR});
	vkUnmapMemory(vulkan.device, resources.sbt_buffer.memory);

	const VkDeviceAddress sbt_address = acceleration_structures::get_buffer_device_address(vulkan, resources.sbt_buffer.handle);
	resources.raygen_region.deviceAddress = sbt_address;
	resources.raygen_region.stride = entry_size;
	resources.raygen_region.size = entry_size;
	resources.callable_region.deviceAddress = sbt_address + callable_offset;
	resources.callable_region.stride = entry_size;
	resources.callable_region.size = entry_size;
}

static void push_addresses(const vulkan_setup_t& vulkan, VkCommandBuffer command_buffer, VkPipelineLayout layout, const PushConstants& push_constants)
{
	if (!vulkan.has_trace_helpers)
	{
		vkCmdPushConstants(
			command_buffer,
			layout,
			VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR,
			0,
			sizeof(push_constants),
			&push_constants);
		return;
	}

	const VkDeviceSize marked_offsets[2] = {
		offsetof(PushConstants, target_address),
		offsetof(PushConstants, decoy_address),
	};
	const VkMarkingTypeARM marking_types[2] = {
		VK_MARKING_TYPE_DEVICE_ADDRESS_ARM,
		VK_MARKING_TYPE_DEVICE_ADDRESS_ARM,
	};
	VkMarkingSubTypeARM sub_types[2]{};
	sub_types[0].deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
	sub_types[1].deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
	VkMarkedOffsetsARM markings{VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr};
	markings.count = 2;
	markings.pOffsets = marked_offsets;
	markings.pMarkingTypes = marking_types;
	markings.pSubTypes = sub_types;

	assert(vulkan.vkCmdPushConstants2);
	VkPushConstantsInfoKHR push_info{VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR, &markings};
	push_info.layout = layout;
	push_info.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR;
	push_info.offset = 0;
	push_info.size = sizeof(push_constants);
	push_info.pValues = &push_constants;
	vulkan.vkCmdPushConstants2(command_buffer, &push_info);
}

static void trace(vulkan_setup_t& vulkan, Resources& resources)
{
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	VkResult result = vkResetCommandBuffer(resources.context.command_buffer, 0);
	check(result);
	result = vkBeginCommandBuffer(resources.context.command_buffer, &begin_info);
	check(result);

	vkCmdBindPipeline(resources.context.command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, resources.pipeline);
	const PushConstants push_constants{
		resources.target_buffer.address.deviceAddress,
		resources.decoy_buffer.address.deviceAddress,
	};
	push_addresses(vulkan, resources.context.command_buffer, resources.pipeline_layout, push_constants);

	const VkStridedDeviceAddressRegionKHR empty_region{};
	resources.context.functions.vkCmdTraceRaysKHR(
		resources.context.command_buffer,
		&resources.raygen_region,
		&empty_region,
		&empty_region,
		&resources.callable_region,
		1,
		1,
		1);

	VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr};
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(
		resources.context.command_buffer,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		VK_PIPELINE_STAGE_HOST_BIT,
		0,
		1,
		&barrier,
		0,
		nullptr,
		0,
		nullptr);

	result = vkEndCommandBuffer(resources.context.command_buffer);
	check(result);
	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &resources.context.command_buffer;

	bench_start_iteration(vulkan.bench);
	result = vkQueueSubmit(resources.context.queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(resources.context.queue);
	check(result);
	bench_stop_iteration(vulkan.bench);
}

static int verify(const vulkan_setup_t& vulkan, const Resources& resources)
{
	uint32_t* target = nullptr;
	uint32_t* decoy = nullptr;
	VkResult result = vkMapMemory(vulkan.device, resources.target_buffer.memory, 0, sizeof(*target), 0, reinterpret_cast<void**>(&target));
	check(result);
	result = vkMapMemory(vulkan.device, resources.decoy_buffer.memory, 0, sizeof(*decoy), 0, reinterpret_cast<void**>(&decoy));
	check(result);

	const bool valid = *target == kWrittenValue && *decoy == 0;
	if (!valid)
	{
		fprintf(stderr, "Callable address result mismatch: target=0x%08x decoy=0x%08x\n", *target, *decoy);
	}
	assert(valid && "Callable shader must return the target buffer address");

	vkUnmapMemory(vulkan.device, resources.decoy_buffer.memory);
	vkUnmapMemory(vulkan.device, resources.target_buffer.memory);

	if (vulkan.vkAssertBuffer)
	{
		uint32_t checksum = 0;
		VkUpdateBufferInfoARM assert_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr};
		assert_info.dstBuffer = resources.target_buffer.handle;
		assert_info.dstOffset = 0;
		assert_info.dataSize = sizeof(uint32_t);
		result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum, "callable shader returned address target");
		check(result);
	}

	return valid ? 0 : 1;
}

static void cleanup(const vulkan_setup_t& vulkan, Resources& resources)
{
	vkDestroyPipeline(vulkan.device, resources.pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, resources.pipeline_layout, nullptr);
	for (VkPipelineShaderStageCreateInfo& stage : resources.shader_stages)
	{
		vkDestroyShaderModule(vulkan.device, stage.module, nullptr);
	}
	acceleration_structures::destroy_buffer(vulkan, resources.sbt_buffer);
	acceleration_structures::destroy_buffer(vulkan, resources.decoy_buffer);
	acceleration_structures::destroy_buffer(vulkan, resources.target_buffer);
	ray_tracing_common::destroy_context(vulkan, resources.context);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray_tracing_features};
	ray_tracing_features.rayTracingPipeline = VK_TRUE;
	acceleration_features.accelerationStructure = VK_TRUE;

	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_MAINTENANCE_6_EXTENSION_NAME);
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
	reqs.reqfeat12.descriptorIndexing = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&acceleration_features);
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.queues = 1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_raytracing_callable_bda", reqs);
	assert(vulkan.hasfeat12.bufferDeviceAddress && "Buffer device address required");

	Resources resources{};
	ray_tracing_common::init_context(vulkan, resources.context);
	create_output_buffers(vulkan, resources);
	create_pipeline(vulkan, resources);
	create_sbt(vulkan, resources);
	trace(vulkan, resources);
	const int result = verify(vulkan, resources);
	cleanup(vulkan, resources);
	test_done(vulkan);
	return result;
}
