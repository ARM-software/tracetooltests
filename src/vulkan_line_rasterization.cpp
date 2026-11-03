#include "vulkan_common.h"

#include "vulkan_dynamic_vertex_state_frag.inc"
#include "vulkan_dynamic_vertex_state_vert.inc"

static constexpr uint32_t width = 32;
static constexpr uint32_t height = 32;
static constexpr VkDeviceSize image_size = width * height * 4;
static const float line_vertices[] = {
	-0.80f, -0.60f, 0.80f, -0.60f,
	-0.80f,  0.00f, 0.80f,  0.00f,
	-0.80f,  0.60f, 0.80f,  0.60f,
	-0.80f,  0.80f, 0.80f,  0.80f,
};

struct buffer_t
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};
static VkPhysicalDeviceLineRasterizationFeaturesEXT requested_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT,
                                                                       nullptr};
static VkPhysicalDeviceLineRasterizationFeaturesEXT supported_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT,
                                                                       nullptr};

static bool line_rasterization_supported(VkPhysicalDevice physical_device)
{
	VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported_features};
	vkGetPhysicalDeviceFeatures2(physical_device, &features);
	requested_features.rectangularLines = supported_features.rectangularLines;
	requested_features.bresenhamLines = supported_features.bresenhamLines;
	requested_features.smoothLines = supported_features.smoothLines;
	requested_features.stippledRectangularLines = supported_features.stippledRectangularLines;
	requested_features.stippledBresenhamLines = supported_features.stippledBresenhamLines;
	requested_features.stippledSmoothLines = supported_features.stippledSmoothLines;
	return supported_features.rectangularLines == VK_TRUE && supported_features.stippledRectangularLines == VK_TRUE;
}

static buffer_t create_buffer(const vulkan_setup_t &vulkan, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags memory_properties, const char *name)
{
	VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	info.size = size;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buffer_t result{};
	check(vkCreateBuffer(vulkan.device, &info, nullptr, &result.buffer));
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)result.buffer, name);
	VkMemoryRequirements requirements{};
	vkGetBufferMemoryRequirements(vulkan.device, result.buffer, &requirements);
	VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, memory_properties);
	check(vkAllocateMemory(vulkan.device, &allocation, nullptr, &result.memory));
	check(vkBindBufferMemory(vulkan.device, result.buffer, result.memory, 0));
	return result;
}

static bool has_color(const uint8_t *pixels)
{
	for (VkDeviceSize i = 0; i < image_size; i += 4)
	{
		if (pixels[i] != 0 || pixels[i + 1] != 0 || pixels[i + 2] != 0)
			return true;
	}
	return false;
}

static bool has_stippled_line(const uint8_t *pixels, uint32_t first_row, uint32_t last_row)
{
	for (uint32_t y = first_row; y <= last_row; y++)
	{
		bool has_colored_pixel = false;
		bool has_black_pixel = false;
		for (uint32_t x = 4; x < width - 4; x++)
		{
			const uint8_t *pixel = pixels + (y * width + x) * 4;
			if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0)
				has_colored_pixel = true;
			else
				has_black_pixel = true;
		}
		if (has_colored_pixel && has_black_pixel) return true;
	}
	return false;
}

static VkPipeline create_line_pipeline(const vulkan_setup_t &vulkan, VkGraphicsPipelineCreateInfo *pipeline_info,
	                                   VkPipelineRasterizationLineStateCreateInfoEXT *line_state,
	                                   VkLineRasterizationModeEXT mode, const char *name)
{
	line_state->lineRasterizationMode = mode;
	VkPipeline pipeline = VK_NULL_HANDLE;
	check(vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, pipeline_info, nullptr, &pipeline));
	assert(pipeline != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, name);
	return pipeline;
}

void create_image(const vulkan_setup_t &vulkan, VkImageCreateInfo *image_info, VkImageView *view, VkDeviceMemory *image_memory,
                  VkImage *image)
{

	image_info->imageType = VK_IMAGE_TYPE_2D;
	image_info->format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info->extent = {width, height, 1};
	image_info->mipLevels = 1;
	image_info->arrayLayers = 1;
	image_info->samples = VK_SAMPLE_COUNT_1_BIT;
	image_info->tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info->sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	check(vkCreateImage(vulkan.device, image_info, nullptr, image));
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)*image, "line_rasterization_image");
	VkMemoryRequirements requirements{};
	vkGetImageMemoryRequirements(vulkan.device, *image, &requirements);
	VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	check(vkAllocateMemory(vulkan.device, &allocation, nullptr, image_memory));
	check(vkBindImageMemory(vulkan.device, *image, *image_memory, 0));
	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	view_info.image = *image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = image_info->format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;

	check(vkCreateImageView(vulkan.device, &view_info, nullptr, view));
	assert(*view != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)*view, "line_rasterization_image_view");
}

void create_renderpass(const vulkan_setup_t &vulkan, VkImageCreateInfo *image_info, VkRenderPass *render_pass)
{
	VkAttachmentDescription attachment{};
	attachment.format = image_info->format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;
	VkSubpassDependency dependencies[2]{};
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr};
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = dependencies;

	check(vkCreateRenderPass(vulkan.device, &render_pass_info, nullptr, render_pass));
	assert(*render_pass != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)*render_pass, "line_rasterization_render_pass");
}

VkFramebuffer create_framebuffer(const vulkan_setup_t &vulkan, VkRenderPass render_pass, VkImageView view)
{
	VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr};
	info.renderPass = render_pass;
	info.attachmentCount = 1;
	info.pAttachments = &view;
	info.width = width;
	info.height = height;
	info.layers = 1;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	check(vkCreateFramebuffer(vulkan.device, &info, nullptr, &framebuffer));
	assert(framebuffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)framebuffer, "line_rasterization_framebuffer");
	return framebuffer;
}

VkShaderModule create_shader_module(const vulkan_setup_t &vulkan, const unsigned char *spirv, unsigned int size, const char *name)
{
	VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	info.codeSize = size;
	info.pCode = reinterpret_cast<const uint32_t *>(spirv);
	VkShaderModule shader = VK_NULL_HANDLE;
	check(vkCreateShaderModule(vulkan.device, &info, nullptr, &shader));
	assert(shader != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shader, name);
	return shader;
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
	reqs.physical_device_supported = line_rasterization_supported;
	reqs.device_extensions.push_back(VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&requested_features);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_line_rasterization", reqs);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetLineStippleEXT);

	VkPhysicalDeviceLineRasterizationPropertiesEXT line_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT,
	                                                               nullptr};
	VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &line_properties};
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties);
	assert(line_properties.lineSubPixelPrecisionBits > 0);

	buffer_t vertices =
	    create_buffer(vulkan, sizeof(line_vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "line_rasterization_vertices");
	void *mapped = nullptr;
	check(vkMapMemory(vulkan.device, vertices.memory, 0, sizeof(line_vertices), 0, &mapped));
	memcpy(mapped, line_vertices, sizeof(line_vertices));
	vkUnmapMemory(vulkan.device, vertices.memory);
	buffer_t readback =
	    create_buffer(vulkan, image_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "line_rasterization_readback");
	VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
	VkImageView view = VK_NULL_HANDLE;
	VkDeviceMemory image_memory = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	create_image(vulkan, &image_info, &view, &image_memory, &image);

	VkRenderPass render_pass = VK_NULL_HANDLE;
	create_renderpass(vulkan, &image_info, &render_pass);
	VkFramebuffer framebuffer = create_framebuffer(vulkan, render_pass, view);
	VkShaderModule vert = create_shader_module(vulkan, vulkan_dynamic_vertex_state_vert_spirv, vulkan_dynamic_vertex_state_vert_spirv_len,
	                                           "line_rasterization_vertex_shader");
	VkShaderModule frag = create_shader_module(vulkan, vulkan_dynamic_vertex_state_frag_spirv, vulkan_dynamic_vertex_state_frag_spirv_len,
	                                           "line_rasterization_fragment_shader");
	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";
	VkVertexInputBindingDescription binding{0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX};
	VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
	VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr};
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &binding;
	vertex_input.vertexAttributeDescriptionCount = 1;
	vertex_input.pVertexAttributeDescriptions = &attribute;
	VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr};
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	VkViewport viewport{0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
	VkRect2D scissor{{0, 0}, {width, height}};
	VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr};
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;
	VkPipelineRasterizationLineStateCreateInfoEXT line_state{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT,
	                                                         nullptr,
	                                                         VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT,
	                                                         VK_TRUE,
	                                                         1,
	                                                         0x00ff};
	VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, &line_state};
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState blend{};
	blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr};
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &blend;
	VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_LINE_STIPPLE_EXT};
	VkPipelineDynamicStateCreateInfo dynamic_state{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr};
	dynamic_state.dynamicStateCount = 1;
	dynamic_state.pDynamicStates = dynamic_states;
	VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	VkPipelineLayout layout = VK_NULL_HANDLE;
	check(vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &layout));
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)layout, "line_rasterization_pipeline_layout");
	VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr};
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = stages;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterization;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = layout;
	pipeline_info.renderPass = render_pass;
	VkPipeline rectangular_pipeline = create_line_pipeline(vulkan, &pipeline_info, &line_state,
	                                                       VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT,
	                                                       "line_rasterization_rectangular_pipeline");
	VkPipeline bresenham_pipeline = VK_NULL_HANDLE;
	if (requested_features.bresenhamLines == VK_TRUE && requested_features.stippledBresenhamLines == VK_TRUE)
	{
		bresenham_pipeline = create_line_pipeline(vulkan, &pipeline_info, &line_state,
		                                              VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT,
		                                              "line_rasterization_bresenham_pipeline");
	}
	VkPipeline smooth_pipeline = VK_NULL_HANDLE;
	if (requested_features.smoothLines == VK_TRUE && requested_features.stippledSmoothLines == VK_TRUE)
	{
		smooth_pipeline = create_line_pipeline(vulkan, &pipeline_info, &line_state,
		                                          VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT,
		                                          "line_rasterization_smooth_pipeline");
	}
	VkPipeline default_pipeline = VK_NULL_HANDLE;
	if (requested_features.stippledRectangularLines == VK_TRUE && properties.properties.limits.strictLines == VK_TRUE)
	{
		default_pipeline = create_line_pipeline(vulkan, &pipeline_info, &line_state,
		                                           VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT,
		                                           "line_rasterization_default_pipeline");
	}

	VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool pool = VK_NULL_HANDLE;
	check(vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &pool));
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)pool, "line_rasterization_command_pool");
	VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	command_info.commandPool = pool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = 1;
	VkCommandBuffer command = VK_NULL_HANDLE;
	check(vkAllocateCommandBuffers(vulkan.device, &command_info, &command));
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command, "line_rasterization_command_buffer");
	bench_start_iteration(vulkan.bench);
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkBeginCommandBuffer(command, &begin_info));
	VkClearValue clear{};
	clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
	VkRenderPassBeginInfo begin_render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr};
	begin_render.renderPass = render_pass;
	begin_render.framebuffer = framebuffer;
	begin_render.renderArea.extent = {width, height};
	begin_render.clearValueCount = 1;
	begin_render.pClearValues = &clear;
	vkCmdBeginRenderPass(command, &begin_render, VK_SUBPASS_CONTENTS_INLINE);
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &offset);
	pf_vkCmdSetLineStippleEXT(command, 1, 0x00ff);
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, rectangular_pipeline);
	vkCmdDraw(command, 2, 1, 0, 0);
	if (bresenham_pipeline != VK_NULL_HANDLE)
	{
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, bresenham_pipeline);
		vkCmdDraw(command, 2, 1, 2, 0);
	}
	if (smooth_pipeline != VK_NULL_HANDLE)
	{
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, smooth_pipeline);
		vkCmdDraw(command, 2, 1, 4, 0);
	}
	if (default_pipeline != VK_NULL_HANDLE)
	{
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, default_pipeline);
		vkCmdDraw(command, 2, 1, 6, 0);
	}
	vkCmdEndRenderPass(command);
	VkBufferImageCopy copy{};
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent = {width, height, 1};
	vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
	check(vkEndCommandBuffer(command));
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	assert(queue != VK_NULL_HANDLE);
	VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &command;
	check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(queue));
	test_marker_mention(vulkan, "Rendered stippled rectangular lines", VK_OBJECT_TYPE_PIPELINE, (uint64_t)rectangular_pipeline);
	if (vulkan.vkAssertBuffer && get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		VkUpdateBufferInfoARM assert_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, readback.buffer, 0, image_size, nullptr};
		uint32_t checksum = 0;
		check(vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum, "line rasterization readback"));
	}
	check(vkMapMemory(vulkan.device, readback.memory, 0, image_size, 0, &mapped));
	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		const uint8_t *pixels = reinterpret_cast<const uint8_t *>(mapped);
		assert(has_color(pixels));
		assert(has_stippled_line(pixels, 4, 8));
		if (bresenham_pipeline != VK_NULL_HANDLE) assert(has_stippled_line(pixels, 14, 18));
		if (smooth_pipeline != VK_NULL_HANDLE) assert(has_stippled_line(pixels, 23, 27));
		if (default_pipeline != VK_NULL_HANDLE) assert(has_stippled_line(pixels, 27, 31));
	}
	vkUnmapMemory(vulkan.device, readback.memory);
	bench_stop_iteration(vulkan.bench);

	vkDestroyCommandPool(vulkan.device, pool, nullptr);
	if (default_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(vulkan.device, default_pipeline, nullptr);
	if (smooth_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(vulkan.device, smooth_pipeline, nullptr);
	if (bresenham_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(vulkan.device, bresenham_pipeline, nullptr);
	vkDestroyPipeline(vulkan.device, rectangular_pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, layout, nullptr);
	vkDestroyShaderModule(vulkan.device, frag, nullptr);
	vkDestroyShaderModule(vulkan.device, vert, nullptr);
	vkDestroyFramebuffer(vulkan.device, framebuffer, nullptr);
	vkDestroyRenderPass(vulkan.device, render_pass, nullptr);
	vkDestroyImageView(vulkan.device, view, nullptr);
	vkDestroyImage(vulkan.device, image, nullptr);
	testFreeMemory(vulkan, image_memory);
	vkDestroyBuffer(vulkan.device, readback.buffer, nullptr);
	testFreeMemory(vulkan, readback.memory);
	vkDestroyBuffer(vulkan.device, vertices.buffer, nullptr);
	testFreeMemory(vulkan, vertices.memory);
	test_done(vulkan);
	return 0;
}
