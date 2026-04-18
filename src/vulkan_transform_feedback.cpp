#include "vulkan_common.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "vulkan_transform_feedback_vert.inc"

namespace
{

constexpr uint32_t kRenderExtent = 1;
constexpr uint32_t kVertexCount = 3;
constexpr VkDeviceSize kTransformStride = sizeof(float) * 4;
constexpr VkDeviceSize kTransformBufferSize = kTransformStride * kVertexCount;
constexpr VkDeviceSize kCounterBufferSize = sizeof(uint32_t);

struct BufferResource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

BufferResource create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage, const char* name)
{
	BufferResource resource;

	VkBufferCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	create_info.size = size;
	create_info.usage = usage;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &create_info, nullptr, &resource.buffer);
	check(result);
	assert(resource.buffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resource.buffer, name);

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, resource.buffer, &requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, resource.buffer, resource.memory, 0);
	check(result);

	return resource;
}

void destroy_buffer(const vulkan_setup_t& vulkan, BufferResource& resource)
{
	if (resource.buffer) vkDestroyBuffer(vulkan.device, resource.buffer, nullptr);
	if (resource.memory) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

VkShaderModule create_shader_module(const vulkan_setup_t& vulkan)
{
	VkShaderModuleCreateInfo create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	create_info.codeSize = vulkan_transform_feedback_vert_spirv_len;
	create_info.pCode = reinterpret_cast<const uint32_t*>(vulkan_transform_feedback_vert_spirv);

	VkShaderModule module = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &create_info, nullptr, &module);
	check(result);
	assert(module != VK_NULL_HANDLE);
	return module;
}

void zero_buffer(const vulkan_setup_t& vulkan, const BufferResource& resource, VkDeviceSize size)
{
	void* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, resource.memory, 0, size, 0, &mapped);
	check(result);
	std::memset(mapped, 0, size);
	vkUnmapMemory(vulkan.device, resource.memory);
}

}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);

	VkPhysicalDeviceTransformFeedbackFeaturesEXT tf_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, nullptr };
	tf_features.transformFeedback = VK_TRUE;
	tf_features.geometryStreams = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&tf_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_transform_feedback", reqs);
	assert(vulkan.device_extensions.count(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME) == 1);

	MAKEDEVICEPROCADDR(vulkan, vkCmdBindTransformFeedbackBuffersEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBeginTransformFeedbackEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdEndTransformFeedbackEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBeginQueryIndexedEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdEndQueryIndexedEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdDrawIndirectByteCountEXT);

	VkPhysicalDeviceTransformFeedbackPropertiesEXT tf_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceProperties2 properties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &tf_properties };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties2);

	if (!tf_properties.transformFeedbackQueries)
	{
		printf("Transform feedback queries are not supported on this device.\n");
		test_done(vulkan);
		return 77;
	}
	if (!tf_properties.transformFeedbackDraw)
	{
		printf("Transform feedback indirect byte-count draws are not supported on this device.\n");
		test_done(vulkan);
		return 77;
	}
	if (tf_properties.maxTransformFeedbackBuffers < 1 || tf_properties.maxTransformFeedbackStreams < 1)
	{
		printf("Transform feedback buffer or stream limits are too small for this test.\n");
		test_done(vulkan);
		return 77;
	}
	if (tf_properties.maxTransformFeedbackBufferDataStride < kTransformStride)
	{
		printf("Transform feedback stride limit is too small for this test.\n");
		test_done(vulkan);
		return 77;
	}

	BufferResource transform_buffer = create_buffer(
		vulkan, kTransformBufferSize, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT, "transform_feedback_output");
	BufferResource counter_buffer = create_buffer(
		vulkan, kCounterBufferSize, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
		"transform_feedback_counter");
	zero_buffer(vulkan, transform_buffer, kTransformBufferSize);
	zero_buffer(vulkan, counter_buffer, kCounterBufferSize);

	VkQueryPoolCreateInfo query_pool_info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr };
	query_pool_info.queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
	query_pool_info.queryCount = 1;

	VkQueryPool query_pool = VK_NULL_HANDLE;
	VkResult result = vkCreateQueryPool(vulkan.device, &query_pool_info, nullptr, &query_pool);
	check(result);

	VkShaderModule shader_module = create_shader_module(vulkan);

	VkPipelineShaderStageCreateInfo shader_stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	shader_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stage.module = shader_module;
	shader_stage.pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr };

	VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr };
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport = {};
	viewport.width = (float)kRenderExtent;
	viewport.height = (float)kRenderExtent;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.extent = { kRenderExtent, kRenderExtent };

	VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr };
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateStreamCreateInfoEXT stream_info = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT, nullptr, 0, 0
	};
	VkPipelineRasterizationStateCreateInfo rasterization = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, &stream_info };
	rasterization.rasterizerDiscardEnable = VK_TRUE;
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr };
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendStateCreateInfo color_blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr };

	VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &pipeline_layout);
	check(result);

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

	VkRenderPassCreateInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr };
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	result = vkCreateRenderPass(vulkan.device, &render_pass_info, nullptr, &render_pass);
	check(result);

	VkGraphicsPipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };
	pipeline_info.stageCount = 1;
	pipeline_info.pStages = &shader_stage;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterization;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.renderPass = render_pass;

	VkPipeline pipeline = VK_NULL_HANDLE;
	result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(result);

	VkFramebufferCreateInfo framebuffer_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr };
	framebuffer_info.renderPass = render_pass;
	framebuffer_info.width = kRenderExtent;
	framebuffer_info.height = kRenderExtent;
	framebuffer_info.layers = 1;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	result = vkCreateFramebuffer(vulkan.device, &framebuffer_info, nullptr, &framebuffer);
	check(result);

	VkCommandPoolCreateInfo command_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_info.queueFamilyIndex = 0;
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &command_pool_info, nullptr, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_info.commandPool = command_pool;
	command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_info, &command_buffer);
	check(result);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkRenderPassBeginInfo begin_render_pass = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
	begin_render_pass.renderPass = render_pass;
	begin_render_pass.framebuffer = framebuffer;
	begin_render_pass.renderArea.extent = { kRenderExtent, kRenderExtent };

	VkDeviceSize transform_offset = 0;
	VkDeviceSize transform_size = kTransformBufferSize;
	VkDeviceSize counter_offset = 0;

	bench_start_iteration(vulkan.bench);

	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	vkCmdResetQueryPool(command_buffer, query_pool, 0, 1);
	vkCmdBeginRenderPass(command_buffer, &begin_render_pass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	pf_vkCmdBindTransformFeedbackBuffersEXT(command_buffer, 0, 1, &transform_buffer.buffer, &transform_offset, &transform_size);
	pf_vkCmdBeginQueryIndexedEXT(command_buffer, query_pool, 0, 0, 0);
	pf_vkCmdBeginTransformFeedbackEXT(command_buffer, 0, 0, nullptr, nullptr);
	vkCmdDraw(command_buffer, kVertexCount, 1, 0, 0);
	pf_vkCmdEndTransformFeedbackEXT(command_buffer, 0, 1, &counter_buffer.buffer, &counter_offset);
	pf_vkCmdEndQueryIndexedEXT(command_buffer, query_pool, 0, 0);
	vkCmdEndRenderPass(command_buffer);

	VkBufferMemoryBarrier counter_to_indirect = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr };
	counter_to_indirect.srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
	counter_to_indirect.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	counter_to_indirect.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	counter_to_indirect.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	counter_to_indirect.buffer = counter_buffer.buffer;
	counter_to_indirect.offset = 0;
	counter_to_indirect.size = kCounterBufferSize;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 1,
	                     &counter_to_indirect, 0, nullptr);

	vkCmdBeginRenderPass(command_buffer, &begin_render_pass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	pf_vkCmdDrawIndirectByteCountEXT(command_buffer, 1, 0, counter_buffer.buffer, 0, 0, (uint32_t)kTransformStride);
	vkCmdEndRenderPass(command_buffer);

	std::array<VkBufferMemoryBarrier, 2> host_barriers = {};
	host_barriers[0] = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr };
	host_barriers[0].srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
	host_barriers[0].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	host_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	host_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	host_barriers[0].buffer = transform_buffer.buffer;
	host_barriers[0].offset = 0;
	host_barriers[0].size = kTransformBufferSize;

	host_barriers[1] = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr };
	host_barriers[1].srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
	host_barriers[1].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	host_barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	host_barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	host_barriers[1].buffer = counter_buffer.buffer;
	host_barriers[1].offset = 0;
	host_barriers[1].size = kCounterBufferSize;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
	                     (uint32_t)host_barriers.size(), host_barriers.data(), 0, nullptr);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(queue);
	check(result);

	std::array<uint64_t, 2> query_results = {};
	result = vkGetQueryPoolResults(vulkan.device, query_pool, 0, 1, sizeof(query_results), query_results.data(), sizeof(query_results),
	                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
	check(result);
	assert(query_results[0] > 0);
	assert(query_results[1] >= query_results[0]);

	uint32_t counter_value = 0;
	void* mapped = nullptr;
	result = vkMapMemory(vulkan.device, counter_buffer.memory, 0, kCounterBufferSize, 0, &mapped);
	check(result);
	std::memcpy(&counter_value, mapped, sizeof(counter_value));
	vkUnmapMemory(vulkan.device, counter_buffer.memory);
	assert(counter_value == kTransformBufferSize);

	std::array<float, 12> captured = {};
	result = vkMapMemory(vulkan.device, transform_buffer.memory, 0, kTransformBufferSize, 0, &mapped);
	check(result);
	std::memcpy(captured.data(), mapped, kTransformBufferSize);
	vkUnmapMemory(vulkan.device, transform_buffer.memory);

	const std::array<float, 12> expected = {
		-0.5f, -0.5f, 0.0f, 1.0f,
		0.5f, -0.5f, 0.0f, 1.0f,
		0.0f, 0.5f, 0.0f, 1.0f,
	};
	for (size_t i = 0; i < captured.size(); i++)
	{
		assert(std::fabs(captured[i] - expected[i]) < 0.0001f);
	}

	test_marker_mention(vulkan, "Executed VK_EXT_transform_feedback commands", VK_OBJECT_TYPE_BUFFER, (uint64_t)transform_buffer.buffer);
	bench_stop_iteration(vulkan.bench);

	vkDestroyQueryPool(vulkan.device, query_pool, nullptr);
	vkDestroyFramebuffer(vulkan.device, framebuffer, nullptr);
	vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyRenderPass(vulkan.device, render_pass, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyShaderModule(vulkan.device, shader_module, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	destroy_buffer(vulkan, counter_buffer);
	destroy_buffer(vulkan, transform_buffer);

	test_done(vulkan);
	return 0;
}
