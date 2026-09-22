#include "vulkan_common.h"
#include "vulkan_raytracing_common.h"
#include "vulkan_raytracing_position.comp.inc"

struct Resources
{
	ray_tracing_common::Context context;
	ray_tracing_common::SimpleAS accel;

	acceleration_structures::Buffer result_buffer;

	VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
	VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};
	VkDescriptorSet descriptor_set{VK_NULL_HANDLE};

	VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
	VkPipeline pipeline{VK_NULL_HANDLE};
	VkShaderModule shader_module{VK_NULL_HANDLE};
};

struct Position
{
	float x;
	float y;
	float z;
	float padding;
};

static const Position kExpectedPositions[3] = {
    {1.0f, 1.0f, 0.0f, 1.0f},
    {-1.0f, 1.0f, 0.0f, 1.0f},
    {0.0f, -1.0f, 0.0f, 1.0f},
};
static void create_result_buffer(const vulkan_setup_t &vulkan, Resources &resources)
{
	Position initial[3] = {
	    {99.0f, 99.0f, 99.0f, 99.0f},
	    {99.0f, 99.0f, 99.0f, 99.0f},
	    {99.0f, 99.0f, 99.0f, 99.0f},
	};

	resources.result_buffer =
	    acceleration_structures::prepare_buffer(vulkan, sizeof(initial), initial, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.result_buffer.handle, "raytracing_position_result");
}

static void create_descriptor_set(const vulkan_setup_t &vulkan, Resources &resources)
{
	VkDescriptorSetLayoutBinding bindings[2]{};

	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	layout_info.bindingCount = 2;
	layout_info.pBindings = bindings;

	check(vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &resources.descriptor_set_layout));

	VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &resources.descriptor_set_layout;

	check(vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &resources.pipeline_layout));
	VkDescriptorPoolSize pool_sizes[2]{};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	pool_sizes[0].descriptorCount = 1;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_sizes[1].descriptorCount = 1;

	VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr};
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 2;
	pool_info.pPoolSizes = pool_sizes;

	check(vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &resources.descriptor_pool));
	VkDescriptorSetAllocateInfo allocate_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr};
	allocate_info.descriptorPool = resources.descriptor_pool;
	allocate_info.descriptorSetCount = 1;
	allocate_info.pSetLayouts = &resources.descriptor_set_layout;

	check(vkAllocateDescriptorSets(vulkan.device, &allocate_info, &resources.descriptor_set));
	VkWriteDescriptorSetAccelerationStructureKHR as_info{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, nullptr};
	as_info.accelerationStructureCount = 1;
	as_info.pAccelerationStructures = &resources.accel.tlas.handle;
	VkDescriptorBufferInfo buffer_info{};
	buffer_info.buffer = resources.result_buffer.handle;
	buffer_info.offset = 0;
	buffer_info.range = sizeof(Position) * 3;

	VkWriteDescriptorSet writes[2]{};

	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].pNext = &as_info;
	writes[0].dstSet = resources.descriptor_set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = resources.descriptor_set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[1].pBufferInfo = &buffer_info;

	vkUpdateDescriptorSets(vulkan.device, 2, writes, 0, nullptr);
}

static void create_pipeline(const vulkan_setup_t &vulkan, Resources &resources)
{
	VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	module_info.codeSize = vulkan_raytracing_position_comp_spv_len;
	module_info.pCode = reinterpret_cast<const uint32_t *>(vulkan_raytracing_position_comp_spv);

	check(vkCreateShaderModule(vulkan.device, &module_info, nullptr, &resources.shader_module));

	VkPipelineShaderStageCreateInfo stage_info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr};
	stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage_info.module = resources.shader_module;
	stage_info.pName = "main";

	VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr};
	pipeline_info.stage = stage_info;
	pipeline_info.layout = resources.pipeline_layout;

	check(vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &resources.pipeline));
}

static void dispatch(const vulkan_setup_t &vulkan, Resources &resources)
{
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	check(vkResetCommandBuffer(resources.context.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.context.command_buffer, &begin_info));

	vkCmdBindPipeline(resources.context.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.pipeline);

	vkCmdBindDescriptorSets(resources.context.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.pipeline_layout, 0, 1,
	                        &resources.descriptor_set, 0, nullptr);

	test_marker_mention(vulkan, "Fetch triangle positions", VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.result_buffer.handle);

	vkCmdDispatch(resources.context.command_buffer, 1, 1, 1);

	check(vkEndCommandBuffer(resources.context.command_buffer));

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &resources.context.command_buffer;

	check(vkQueueSubmit(resources.context.queue, 1, &submit_info, VK_NULL_HANDLE));

	check(vkQueueWaitIdle(resources.context.queue));
}

static void verify(const vulkan_setup_t &vulkan, Resources &resources)
{
	if (get_env_int("TOOLSTEST_NULL_RUN", 0))
	{
		return;
	}

	void *mapped = nullptr;
	check(vkMapMemory(vulkan.device, resources.result_buffer.memory, 0, sizeof(kExpectedPositions), 0, &mapped));

	assert(mapped != nullptr);

	const Position *actual = static_cast<const Position *>(mapped);

	for (uint32_t i = 0; i < 3; ++i)
	{
		assert(actual[i].x == kExpectedPositions[i].x);
		assert(actual[i].y == kExpectedPositions[i].y);
		assert(actual[i].z == kExpectedPositions[i].z);
		assert(actual[i].padding == kExpectedPositions[i].padding);
	}

	vkUnmapMemory(vulkan.device, resources.result_buffer.memory);
	if (vulkan.vkAssertBuffer)
	{
		uint32_t checksum = 0;

		VkUpdateBufferInfoARM assert_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr};
		assert_info.dstBuffer = resources.result_buffer.handle;
		assert_info.dstOffset = 0;
		assert_info.dataSize = sizeof(kExpectedPositions);

		check(vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum, "Fetched triangle positions"));
	}
}

static void cleanup(const vulkan_setup_t &vulkan, Resources &resources)
{
	vkDestroyPipeline(vulkan.device, resources.pipeline, nullptr);
	vkDestroyShaderModule(vulkan.device, resources.shader_module, nullptr);
	vkDestroyPipelineLayout(vulkan.device, resources.pipeline_layout, nullptr);
	vkDestroyDescriptorPool(vulkan.device, resources.descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, resources.descriptor_set_layout, nullptr);

	acceleration_structures::destroy_buffer(vulkan, resources.result_buffer);

	ray_tracing_common::destroy_simple_triangle_as(vulkan, resources.context, resources.accel);
	ray_tracing_common::destroy_context(vulkan, resources.context);
}

static bool position_fetch_supported(VkPhysicalDevice physical_device)
{
	VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR position_fetch_features{
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR};
	VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &position_fetch_features};
	vkGetPhysicalDeviceFeatures2(physical_device, &features);
	if (!position_fetch_features.rayTracingPositionFetch)
	{
		printf("VK_KHR_ray_tracing_position_fetch feature is not supported\n");
		return false;
	}
	return true;
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	reqs.bufferDeviceAddress = true;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.physical_device_supported = position_fetch_supported;

	VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR position_fetch_features{
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR, nullptr, VK_TRUE};
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
	                                                       &position_fetch_features, VK_TRUE};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray_query_features, VK_TRUE};
	reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&acceleration_features);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_raytracing_position", reqs);

	Resources resources{};

	ray_tracing_common::init_context(vulkan, resources.context);

	ray_tracing_common::build_simple_triangle_as(vulkan, resources.context, resources.accel,
	                                             VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
	                                                 VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR);

	create_result_buffer(vulkan, resources);
	create_descriptor_set(vulkan, resources);
	create_pipeline(vulkan, resources);
	dispatch(vulkan, resources);
	verify(vulkan, resources);
	cleanup(vulkan, resources);

	test_done(vulkan);
}
