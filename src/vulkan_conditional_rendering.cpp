#include "vulkan_common.h"

#include "vulkan_dynamic_vertex_state_frag.inc"
#include "vulkan_dynamic_vertex_state_vert.inc"

const uint32_t target_width = 32;
const uint32_t target_height = 32;

const float positions[] = {
    0.0f, -0.75f, 0.75f, 0.75f, -0.75f, 0.75f,
};

constexpr VkDeviceSize readback_size = target_width * target_height * 4;

static bool region_has_green(const uint8_t *data, uint32_t first_column, uint32_t column_count)
{
	for (uint32_t y = 0; y < target_height; y++)
	{
		for (uint32_t x = first_column; x < first_column + column_count; x++)
		{
			const uint8_t *pixel = data + (y * target_width + x) * 4;
			if (pixel[1] > pixel[0] && pixel[1] > pixel[2])
				return true;
		}
	}
	return false;
}

static bool conditional_rendering_supported(VkPhysicalDevice physical_device)
{
	VkPhysicalDeviceConditionalRenderingFeaturesEXT supported_featured = {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT, nullptr};
	VkPhysicalDeviceFeatures2 device_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported_featured};
	vkGetPhysicalDeviceFeatures2(physical_device, &device_features);
	return supported_featured.conditionalRendering;
}

VkPipeline create_pipeline(const vulkan_setup_t &vulkan, VkRenderPass render_pass, VkPipelineLayout pipeline_layout,
                           VkShaderModule vertex_shader, VkShaderModule fragment_shader, uint32_t viewport_x, uint32_t viewport_width,
                           const char *name)
{
	VkPipelineShaderStageCreateInfo shader_stages[2]{};
	shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages[0].module = vertex_shader;
	shader_stages[0].pName = "main";
	shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages[1].module = fragment_shader;
	shader_stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr};
	VkVertexInputBindingDescription binding_description{};
	binding_description.binding = 0;
	binding_description.stride = sizeof(float) * 2;
	binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attribute_description{};
	attribute_description.location = 0;
	attribute_description.binding = 0;
	attribute_description.format = VK_FORMAT_R32G32_SFLOAT;
	attribute_description.offset = 0;

	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &binding_description;
	vertex_input.vertexAttributeDescriptionCount = 1;
	vertex_input.pVertexAttributeDescriptions = &attribute_description;

	VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{static_cast<float>(viewport_x),    0.0f, static_cast<float>(viewport_width),
	                    static_cast<float>(target_height), 0.0f, 1.0f};
	VkRect2D scissor{{static_cast<int32_t>(viewport_x), 0}, {viewport_width, target_height}};
	VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr};
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr};
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blend_attachment{};
	blend_attachment.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr};
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &blend_attachment;

	VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr};
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
	pipeline_info.subpass = 0;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(result);
	assert(pipeline != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, name);
	return pipeline;
}

VkResult set_up_command_buffer(vulkan_setup_t vulkan, VkCommandBuffer *command_buffer, VkCommandPool *command_pool)
{

	VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = vulkan.queue_family_index;

	VkResult result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, command_pool);
	check(result);

	VkCommandBufferAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	alloc_info.commandPool = *command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;

	result = vkAllocateCommandBuffers(vulkan.device, &alloc_info, command_buffer);
	return result;
}

void create_render_pass(const vulkan_setup_t &vulkan, VkRenderPass *render_pass)
{
	VkAttachmentDescription color_attachment{};
	color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	VkAttachmentReference color_reference{};
	color_reference.attachment = 0;
	color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_reference;

	VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr};
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	check(vkCreateRenderPass(vulkan.device, &render_pass_info, nullptr, render_pass));
	assert(*render_pass != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)*render_pass, "conditional_rendering_render_pass");
}

VkFramebuffer create_framebuffer(const vulkan_setup_t &vulkan, VkRenderPass render_pass, VkImageView image_view)
{
	VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr};
	framebuffer_info.renderPass = render_pass;
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = &image_view;
	framebuffer_info.width = target_width;
	framebuffer_info.height = target_height;
	framebuffer_info.layers = 1;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VkResult result = vkCreateFramebuffer(vulkan.device, &framebuffer_info, nullptr, &framebuffer);
	check(result);
	assert(framebuffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)framebuffer, "dynamic_vertex_state_framebuffer");
	return framebuffer;
}

VkPipelineLayout create_pipeline_layout(const vulkan_setup_t &vulkan)
{
	VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkResult result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &pipeline_layout);
	check(result);
	assert(pipeline_layout != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipeline_layout, "dynamic_vertex_state_pipeline_layout");
	return pipeline_layout;
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back(VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME);
	reqs.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
	reqs.physical_device_supported = conditional_rendering_supported;
	VkPhysicalDeviceConditionalRenderingFeaturesEXT requested_features{};
	requested_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT;
	requested_features.pNext = nullptr;
	requested_features.conditionalRendering = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&requested_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_conditional_rendering", reqs);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBeginConditionalRenderingEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdEndConditionalRenderingEXT);

	VkBuffer buffer;
	VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	bufferCreateInfo.size = 32;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer);
	assert(result == VK_SUCCESS);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, "buffer");

	VkImage color_image = VK_NULL_HANDLE;
	VkDeviceMemory color_memory = VK_NULL_HANDLE;
	VkImageView color_view = VK_NULL_HANDLE;
	VkRenderPass render_pass = VK_NULL_HANDLE;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline normal_pipeline = VK_NULL_HANDLE;
	VkPipeline inverted_pipeline = VK_NULL_HANDLE;
	VkPipeline disabled_pipeline = VK_NULL_HANDLE;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan.device, buffer, &memory_requirements);
	VkMemoryPropertyFlags memoryflags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	const VkDeviceSize offset = memory_requirements.alignment;
	assert(offset > 0);
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, memoryflags);
	VkDeviceSize aligned_size = offset + memory_requirements.size;

	VkMemoryAllocateInfo pAllocateMemInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_size;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(result);
	assert(memory != VK_NULL_HANDLE);
	result = vkBindBufferMemory(vulkan.device, buffer, memory, offset);
	check(result);

	char *data = nullptr;
	const uint32_t conditions[] = {1, 0, 0};
	result = vkMapMemory(vulkan.device, memory, offset, sizeof(conditions), 0, (void **)&data);
	check(result);
	memcpy(data, conditions, sizeof(conditions));
	if (vulkan.has_explicit_host_updates)
		testFlushMemory(vulkan, memory, offset, sizeof(conditions), true);
	vkUnmapMemory(vulkan.device, memory);

	VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent = {target_width, target_height, 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	check(vkCreateImage(vulkan.device, &image_info, nullptr, &color_image));
	assert(color_image != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)color_image, "conditional_rendering_color_image");

	VkMemoryRequirements image_requirements{};
	vkGetImageMemoryRequirements(vulkan.device, color_image, &image_requirements);

	VkMemoryAllocateInfo allocation_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocation_info.allocationSize = image_requirements.size;
	allocation_info.memoryTypeIndex = get_device_memory_type(image_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	check(vkAllocateMemory(vulkan.device, &allocation_info, nullptr, &color_memory));
	check(vkBindImageMemory(vulkan.device, color_image, color_memory, 0));

	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	view_info.image = color_image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;

	check(vkCreateImageView(vulkan.device, &view_info, nullptr, &color_view));

	create_render_pass(vulkan, &render_pass);
	framebuffer = create_framebuffer(vulkan, render_pass, color_view);
	assert(framebuffer != VK_NULL_HANDLE);
	pipeline_layout = create_pipeline_layout(vulkan);
	VkShaderModuleCreateInfo vertex_shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	vertex_shader_info.codeSize = vulkan_dynamic_vertex_state_vert_spirv_len;
	vertex_shader_info.pCode = reinterpret_cast<const uint32_t *>(vulkan_dynamic_vertex_state_vert_spirv);
	VkShaderModule vertex_shader = VK_NULL_HANDLE;
	result = vkCreateShaderModule(vulkan.device, &vertex_shader_info, nullptr, &vertex_shader);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)vertex_shader, "dynamic_vertex_state_vertex_shader");

	VkShaderModuleCreateInfo fragment_shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	fragment_shader_info.codeSize = vulkan_dynamic_vertex_state_frag_spirv_len;
	fragment_shader_info.pCode = reinterpret_cast<const uint32_t *>(vulkan_dynamic_vertex_state_frag_spirv);
	VkShaderModule fragment_shader = VK_NULL_HANDLE;
	result = vkCreateShaderModule(vulkan.device, &fragment_shader_info, nullptr, &fragment_shader);
	check(result);
	assert(fragment_shader != VK_NULL_HANDLE);
	assert(vertex_shader != VK_NULL_HANDLE);
	normal_pipeline = create_pipeline(vulkan, render_pass, pipeline_layout, vertex_shader, fragment_shader, 0, 10,
	                                  "conditional_rendering_normal_pipeline");
	inverted_pipeline = create_pipeline(vulkan, render_pass, pipeline_layout, vertex_shader, fragment_shader, 11, 10,
	                                    "conditional_rendering_inverted_pipeline");
	disabled_pipeline = create_pipeline(vulkan, render_pass, pipeline_layout, vertex_shader, fragment_shader, 22, 10,
	                                    "conditional_rendering_disabled_pipeline");
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)color_view, "conditional_rendering_color_view");

	VkBuffer vertex_buffer = VK_NULL_HANDLE;
	VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
	VkBufferCreateInfo vertex_buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	vertex_buffer_info.size = sizeof(positions);
	vertex_buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vertex_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	check(vkCreateBuffer(vulkan.device, &vertex_buffer_info, nullptr, &vertex_buffer));
	assert(vertex_buffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)vertex_buffer, "conditional_rendering_vertex_buffer");

	VkMemoryRequirements vertex_requirements{};
	vkGetBufferMemoryRequirements(vulkan.device, vertex_buffer, &vertex_requirements);
	VkMemoryAllocateInfo vertex_allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	vertex_allocation.allocationSize = vertex_requirements.size;
	vertex_allocation.memoryTypeIndex = get_device_memory_type(vertex_requirements.memoryTypeBits,
	                                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	check(vkAllocateMemory(vulkan.device, &vertex_allocation, nullptr, &vertex_memory));
	assert(vertex_memory != VK_NULL_HANDLE);
	check(vkBindBufferMemory(vulkan.device, vertex_buffer, vertex_memory, 0));

	void *mapped = nullptr;
	check(vkMapMemory(vulkan.device, vertex_memory, 0, sizeof(positions), 0, &mapped));
	assert(mapped != nullptr);
	memcpy(mapped, positions, sizeof(positions));
	if (vulkan.has_explicit_host_updates)
		testFlushMemory(vulkan, vertex_memory, 0, sizeof(positions), true);
	vkUnmapMemory(vulkan.device, vertex_memory);

	VkBuffer readback_buffer = VK_NULL_HANDLE;
	VkDeviceMemory readback_memory = VK_NULL_HANDLE;
	VkBufferCreateInfo readback_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	readback_info.size = readback_size;
	readback_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	readback_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	check(vkCreateBuffer(vulkan.device, &readback_info, nullptr, &readback_buffer));
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)readback_buffer, "conditional_rendering_readback");
	VkMemoryRequirements readback_requirements{};
	vkGetBufferMemoryRequirements(vulkan.device, readback_buffer, &readback_requirements);
	VkMemoryAllocateInfo readback_allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	readback_allocation.allocationSize = readback_requirements.size;
	readback_allocation.memoryTypeIndex = get_device_memory_type(
	    readback_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	check(vkAllocateMemory(vulkan.device, &readback_allocation, nullptr, &readback_memory));
	check(vkBindBufferMemory(vulkan.device, readback_buffer, readback_memory, 0));

	check(set_up_command_buffer(vulkan, &command_buffer, &command_pool));

	bench_start_iteration(vulkan.bench);
	VkCommandBufferBeginInfo command_buffer_begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	command_buffer_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(vkBeginCommandBuffer(command_buffer, &command_buffer_begin));

	VkBufferMemoryBarrier condition_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
	condition_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	condition_barrier.dstAccessMask = VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;
	condition_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	condition_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	condition_barrier.buffer = buffer;
	condition_barrier.offset = 0;
	condition_barrier.size = VK_WHOLE_SIZE;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT, 0, 0, nullptr, 1,
	                     &condition_barrier, 0, nullptr);

	VkClearValue clear_value{};
	clear_value.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
	VkRenderPassBeginInfo render_pass_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr};
	render_pass_begin.renderPass = render_pass;
	render_pass_begin.framebuffer = framebuffer;
	render_pass_begin.renderArea.extent = {target_width, target_height};
	render_pass_begin.clearValueCount = 1;
	render_pass_begin.pClearValues = &clear_value;
	vkCmdBeginRenderPass(command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
	const VkDeviceSize vertex_offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vertex_offset);

	VkConditionalRenderingBeginInfoEXT condition_info{VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT, nullptr};
	condition_info.buffer = buffer;
	condition_info.offset = 0;
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, normal_pipeline);
	pf_vkCmdBeginConditionalRenderingEXT(command_buffer, &condition_info);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	pf_vkCmdEndConditionalRenderingEXT(command_buffer);
	condition_info.offset = sizeof(uint32_t);
	condition_info.flags = VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, inverted_pipeline);
	pf_vkCmdBeginConditionalRenderingEXT(command_buffer, &condition_info);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	pf_vkCmdEndConditionalRenderingEXT(command_buffer);
	condition_info.offset = sizeof(uint32_t) * 2;
	condition_info.flags = 0;
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, disabled_pipeline);
	pf_vkCmdBeginConditionalRenderingEXT(command_buffer, &condition_info);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	pf_vkCmdEndConditionalRenderingEXT(command_buffer);
	vkCmdEndRenderPass(command_buffer);

	VkMemoryBarrier color_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr};
	color_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	color_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
	                     &color_barrier, 0, nullptr, 0, nullptr);
	VkBufferImageCopy copy_region{};
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent = {target_width, target_height, 1};
	vkCmdCopyImageToBuffer(command_buffer, color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buffer, 1, &copy_region);
	check(vkEndCommandBuffer(command_buffer));

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	assert(queue != VK_NULL_HANDLE);
	VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr};
	VkFence fence = VK_NULL_HANDLE;
	check(vkCreateFence(vulkan.device, &fence_info, nullptr, &fence));
	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	check(vkQueueSubmit(queue, 1, &submit_info, fence));
	check(vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX));
	if (vulkan.vkAssertBuffer)
	{
		uint32_t readback_crc = 0;
		VkUpdateBufferInfoARM assert_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, readback_buffer, 0, readback_size, nullptr};
		check(vulkan.vkAssertBuffer(vulkan.device, &assert_info, &readback_crc, "conditional rendering readback"));
		(void)readback_crc;
	}
	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		void *readback_data = nullptr;
		check(vkMapMemory(vulkan.device, readback_memory, 0, readback_size, 0, &readback_data));
		const uint8_t *image_data = static_cast<const uint8_t *>(readback_data);
		assert(region_has_green(image_data, 0, 10));
		assert(region_has_green(image_data, 11, 10));
		assert(!region_has_green(image_data, 22, 10));
		vkUnmapMemory(vulkan.device, readback_memory);
	}
	vkDestroyFence(vulkan.device, fence, nullptr);
	bench_stop_iteration(vulkan.bench);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyBuffer(vulkan.device, vertex_buffer, nullptr);
	vkFreeMemory(vulkan.device, vertex_memory, nullptr);
	vkDestroyBuffer(vulkan.device, readback_buffer, nullptr);
	vkFreeMemory(vulkan.device, readback_memory, nullptr);
	vkDestroyPipeline(vulkan.device, disabled_pipeline, nullptr);
	vkDestroyPipeline(vulkan.device, inverted_pipeline, nullptr);
	vkDestroyPipeline(vulkan.device, normal_pipeline, nullptr);
	vkDestroyShaderModule(vulkan.device, fragment_shader, nullptr);
	vkDestroyShaderModule(vulkan.device, vertex_shader, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyFramebuffer(vulkan.device, framebuffer, nullptr);
	vkDestroyRenderPass(vulkan.device, render_pass, nullptr);
	vkDestroyImageView(vulkan.device, color_view, nullptr);
	vkDestroyImage(vulkan.device, color_image, nullptr);
	vkFreeMemory(vulkan.device, color_memory, nullptr);
	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	vkFreeMemory(vulkan.device, memory, nullptr);
	test_done(vulkan);
	return 0;
}
