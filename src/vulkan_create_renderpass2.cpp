#include "vulkan_common.h"

#include "vulkan_dynamic_rendering_frag.inc"
#include "vulkan_dynamic_rendering_vert.inc"

#include <array>
#include <cstdint>

namespace
{

constexpr uint32_t kWidth = 32;
constexpr uint32_t kHeight = 32;
constexpr VkDeviceSize kReadbackSize = kWidth * kHeight * 4;

struct BufferResource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct ImageResource
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
};

static BufferResource create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage, const char* name)
{
	BufferResource resource{};

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
	allocate_info.memoryTypeIndex = get_device_memory_type(
		requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, resource.buffer, resource.memory, 0);
	check(result);

	return resource;
}

static void destroy_buffer(const vulkan_setup_t& vulkan, BufferResource& resource)
{
	if (resource.buffer) vkDestroyBuffer(vulkan.device, resource.buffer, nullptr);
	if (resource.memory) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

static ImageResource create_color_target(const vulkan_setup_t& vulkan)
{
	ImageResource resource{};

	VkImageCreateInfo create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	create_info.imageType = VK_IMAGE_TYPE_2D;
	create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	create_info.extent = { kWidth, kHeight, 1 };
	create_info.mipLevels = 1;
	create_info.arrayLayers = 1;
	create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage(vulkan.device, &create_info, nullptr, &resource.image);
	check(result);
	assert(resource.image != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)resource.image, "create_renderpass2_color");

	VkMemoryRequirements requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, resource.image, &requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);

	result = vkBindImageMemory(vulkan.device, resource.image, resource.memory, 0);
	check(result);

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
	view_info.image = resource.image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = create_info.format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;

	result = vkCreateImageView(vulkan.device, &view_info, nullptr, &resource.view);
	check(result);
	assert(resource.view != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)resource.view, "create_renderpass2_color_view");

	return resource;
}

static void destroy_image(const vulkan_setup_t& vulkan, ImageResource& resource)
{
	if (resource.view) vkDestroyImageView(vulkan.device, resource.view, nullptr);
	if (resource.image) vkDestroyImage(vulkan.device, resource.image, nullptr);
	if (resource.memory) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

static VkShaderModule create_shader_module(const vulkan_setup_t& vulkan, const unsigned char* code, unsigned int code_size)
{
	VkShaderModuleCreateInfo create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	create_info.codeSize = code_size;
	create_info.pCode = reinterpret_cast<const uint32_t*>(code);

	VkShaderModule module = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &create_info, nullptr, &module);
	check(result);
	assert(module != VK_NULL_HANDLE);
	return module;
}

static VkPipeline create_graphics_pipeline(const vulkan_setup_t& vulkan, VkPipelineLayout pipeline_layout, VkRenderPass render_pass,
                                           uint32_t subpass, VkShaderModule vert, VkShaderModule frag, const char* name)
{
	VkPipelineShaderStageCreateInfo shader_stages[2] = {};
	shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages[0].module = vert;
	shader_stages[0].pName = "main";
	shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages[1].module = frag;
	shader_stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr };

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr
	};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport = { 0.0f, 0.0f, (float)kWidth, (float)kHeight, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { kWidth, kHeight } };
	VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr };
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterization = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr
	};
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = {
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr
	};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blend_attachment = {};
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo color_blend = {
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr
	};
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &blend_attachment;

	VkGraphicsPipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = shader_stages;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterization;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.renderPass = render_pass;
	pipeline_info.subpass = subpass;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(result);
	assert(pipeline != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, name);
	return pipeline;
}

static void image_barrier(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static bool image_has_color(const uint8_t* data, size_t size)
{
	for (size_t i = 0; i + 3 < size; i += 4)
	{
		if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 0) return true;
	}
	return false;
}

}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.maxApiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_create_renderpass2", reqs);
	assert(vulkan.device_extensions.count(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME) == 1);

	MAKEDEVICEPROCADDR(vulkan, vkCreateRenderPass2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBeginRenderPass2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdNextSubpass2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdEndRenderPass2KHR);

	BufferResource readback = create_buffer(vulkan, kReadbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "create_renderpass2_readback");
	ImageResource color_target = create_color_target(vulkan);

	VkShaderModule vert = create_shader_module(vulkan, vulkan_dynamic_rendering_vert_spv, vulkan_dynamic_rendering_vert_spv_len);
	VkShaderModule frag = create_shader_module(vulkan, vulkan_dynamic_rendering_frag_spv, vulkan_dynamic_rendering_frag_spv_len);

	VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkResult result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &pipeline_layout);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipeline_layout, "create_renderpass2_pipeline_layout");

	VkAttachmentDescription2 color_attachment = { VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2, nullptr };
	color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference2 color_reference = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr };
	color_reference.attachment = 0;
	color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color_reference.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	VkSubpassDescription2 subpasses[2] = {};
	for (VkSubpassDescription2& subpass : subpasses)
	{
		subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_reference;
	}

	VkSubpassDependency2 dependencies[2] = {};
	for (VkSubpassDependency2& dependency : dependencies)
	{
		dependency.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	}
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = 1;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo2 render_pass_info = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2, nullptr, 0, 1, &color_attachment, 2, subpasses, 2, dependencies, 0, nullptr
	};

	VkRenderPass render_pass = VK_NULL_HANDLE;
	result = pf_vkCreateRenderPass2KHR(vulkan.device, &render_pass_info, nullptr, &render_pass);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)render_pass, "create_renderpass2_render_pass");

	VkFramebufferCreateInfo framebuffer_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr };
	framebuffer_info.renderPass = render_pass;
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = &color_target.view;
	framebuffer_info.width = kWidth;
	framebuffer_info.height = kHeight;
	framebuffer_info.layers = 1;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	result = vkCreateFramebuffer(vulkan.device, &framebuffer_info, nullptr, &framebuffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)framebuffer, "create_renderpass2_framebuffer");

	std::array<VkPipeline, 2> pipelines = {
		create_graphics_pipeline(vulkan, pipeline_layout, render_pass, 0, vert, frag, "create_renderpass2_pipeline_0"),
		create_graphics_pipeline(vulkan, pipeline_layout, render_pass, 1, vert, frag, "create_renderpass2_pipeline_1")
	};

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = 0;

	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)command_pool, "create_renderpass2_command_pool");

	VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_info.commandPool = command_pool;
	command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_info, &command_buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer, "create_renderpass2_command_buffer");

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	image_barrier(command_buffer, color_target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	              0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	VkClearValue clear_value = {};
	clear_value.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo begin_render_pass = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
	begin_render_pass.renderPass = render_pass;
	begin_render_pass.framebuffer = framebuffer;
	begin_render_pass.renderArea.extent = { kWidth, kHeight };
	begin_render_pass.clearValueCount = 1;
	begin_render_pass.pClearValues = &clear_value;

	VkSubpassBeginInfo begin_subpass = { VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO, nullptr, VK_SUBPASS_CONTENTS_INLINE };
	VkSubpassEndInfo end_subpass = { VK_STRUCTURE_TYPE_SUBPASS_END_INFO, nullptr };

	pf_vkCmdBeginRenderPass2KHR(command_buffer, &begin_render_pass, &begin_subpass);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[0]);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	pf_vkCmdNextSubpass2KHR(command_buffer, &begin_subpass, &end_subpass);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[1]);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	pf_vkCmdEndRenderPass2KHR(command_buffer, &end_subpass);

	image_barrier(command_buffer, color_target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
	              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy copy_region = {};
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.mipLevel = 0;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent = { kWidth, kHeight, 1 };
	vkCmdCopyImageToBuffer(command_buffer, color_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy_region);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	bench_start_iteration(vulkan.bench);
	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(queue);
	check(result);
	bench_stop_iteration(vulkan.bench);

	void* mapped = nullptr;
	result = vkMapMemory(vulkan.device, readback.memory, 0, kReadbackSize, 0, &mapped);
	check(result);
	assert(image_has_color(reinterpret_cast<const uint8_t*>(mapped), kReadbackSize));
	vkUnmapMemory(vulkan.device, readback.memory);

	test_marker_mention(vulkan, "Executed VK_KHR_create_renderpass2 render pass", VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)render_pass);

	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	for (VkPipeline pipeline : pipelines) vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyFramebuffer(vulkan.device, framebuffer, nullptr);
	vkDestroyRenderPass(vulkan.device, render_pass, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyShaderModule(vulkan.device, frag, nullptr);
	vkDestroyShaderModule(vulkan.device, vert, nullptr);

	destroy_image(vulkan, color_target);
	destroy_buffer(vulkan, readback);

	test_done(vulkan);
	return 0;
}
