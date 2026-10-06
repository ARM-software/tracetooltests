#include "vulkan_common.h"

#include "vulkan_dynamic_vertex_state_frag.inc"
#include "vulkan_dynamic_vertex_state_vert.inc"

const float positions[] = {
    0.0f, -0.75f, 0.75f, 0.75f, -0.75f, 0.75f,
};
constexpr uint32_t target_width = 32;
constexpr uint32_t target_height = 32;
constexpr VkDeviceSize target_size = target_width * target_height * 4;

static bool vertex_input_dynamic_state_supported(VkPhysicalDevice physical_device)
{
	VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT supported_featured = {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT, nullptr};
	VkPhysicalDeviceFeatures2 device_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported_featured};
	vkGetPhysicalDeviceFeatures2(physical_device, &device_features);
	return supported_featured.vertexInputDynamicState;
}

void create_image(const vulkan_setup_t &vulkan, VkImage *image, VkDeviceMemory *memory, VkImageView *view)
{
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

	VkResult result = vkCreateImage(vulkan.device, &image_info, nullptr, image);
	check(result);
	assert(*image != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)*image, "dynamic_vertex_state_color_image");

	VkMemoryRequirements memory_requirements{};
	vkGetImageMemoryRequirements(vulkan.device, *image, &memory_requirements);

	VkMemoryAllocateInfo allocation_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocation_info.allocationSize = memory_requirements.size;
	allocation_info.memoryTypeIndex =
	    get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	result = vkAllocateMemory(vulkan.device, &allocation_info, nullptr, memory);
	check(result);
	assert(*memory != VK_NULL_HANDLE);

	result = vkBindImageMemory(vulkan.device, *image, *memory, 0);
	check(result);

	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	view_info.image = *image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = image_info.format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;

	result = vkCreateImageView(vulkan.device, &view_info, nullptr, view);
	check(result);
	assert(*view != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)*view, "dynamic_vertex_state_color_view");
}

VkRenderPass create_render_pass(const vulkan_setup_t &vulkan)
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

	VkSubpassDependency dependencies[2]{};
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

	VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr};
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = dependencies;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	VkResult result = vkCreateRenderPass(vulkan.device, &render_pass_info, nullptr, &render_pass);
	check(result);
	assert(render_pass != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)render_pass,
	              "dynamic_vertex_state_render_pass");
	return render_pass;
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
	test_set_name(vulkan, VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)framebuffer,
	              "dynamic_vertex_state_framebuffer");
	return framebuffer;
}

VkPipelineLayout create_pipeline_layout(const vulkan_setup_t &vulkan)
{
	VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkResult result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &pipeline_layout);
	check(result);
	assert(pipeline_layout != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipeline_layout,
	              "dynamic_vertex_state_pipeline_layout");
	return pipeline_layout;
}

VkPipeline create_pipeline(const vulkan_setup_t &vulkan, VkRenderPass render_pass, VkPipelineLayout pipeline_layout,
	                       VkShaderModule vertex_shader, VkShaderModule fragment_shader)
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

	VkPipelineInputAssemblyStateCreateInfo input_assembly{
	    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{0.0f, 0.0f, static_cast<float>(target_width), static_cast<float>(target_height), 0.0f, 1.0f};
	VkRect2D scissor{{0, 0}, {target_width, target_height}};
	VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr};
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterization{
	    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr};
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blend_attachment{};
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo color_blend{
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr};
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &blend_attachment;

	VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VERTEX_INPUT_EXT};
	VkPipelineDynamicStateCreateInfo dynamic_state{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr};
	dynamic_state.dynamicStateCount = 1;
	dynamic_state.pDynamicStates = dynamic_states;

	VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr};
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = shader_stages;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterization;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.renderPass = render_pass;
	pipeline_info.subpass = 0;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(result);
	assert(pipeline != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, "dynamic_vertex_state_pipeline");
	return pipeline;
}

void set_up_memory(VkDevice device, VkBuffer *vertex_buffer, VkDeviceMemory *vertex_memory)
{
	VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	buffer_info.size = sizeof(positions);
	buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(device, &buffer_info, nullptr, vertex_buffer);
	check(result);
	assert(*vertex_buffer != VK_NULL_HANDLE);

	VkMemoryRequirements memory_requirements{};
	vkGetBufferMemoryRequirements(device, *vertex_buffer, &memory_requirements);

	VkMemoryAllocateInfo allocation_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocation_info.allocationSize = memory_requirements.size;
	allocation_info.memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits,
	                                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	result = vkAllocateMemory(device, &allocation_info, nullptr, vertex_memory);
	check(result);
	assert(*vertex_memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(device, *vertex_buffer, *vertex_memory, 0);
	check(result);

	void *mapped = nullptr;
	result = vkMapMemory(device, *vertex_memory, 0, sizeof(positions), 0, &mapped);
	check(result);
	assert(mapped != nullptr);

	memcpy(mapped, positions, sizeof(positions));
	vkUnmapMemory(device, *vertex_memory);
}

void create_readback_buffer(const vulkan_setup_t &vulkan, VkBuffer *buffer, VkDeviceMemory *memory)
{
	VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	buffer_info.size = target_size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, buffer);
	check(result);
	assert(*buffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)*buffer, "dynamic_vertex_state_readback");

	VkMemoryRequirements memory_requirements{};
	vkGetBufferMemoryRequirements(vulkan.device, *buffer, &memory_requirements);
	VkMemoryAllocateInfo allocation_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocation_info.allocationSize = memory_requirements.size;
	allocation_info.memoryTypeIndex = get_device_memory_type(
	    memory_requirements.memoryTypeBits,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	result = vkAllocateMemory(vulkan.device, &allocation_info, nullptr, memory);
	check(result);
	assert(*memory != VK_NULL_HANDLE);
	result = vkBindBufferMemory(vulkan.device, *buffer, *memory, 0);
	check(result);
}

bool image_has_green(const uint8_t *data)
{
	for (VkDeviceSize i = 0; i < target_size; i += 4)
	{
		if (data[i + 1] > data[i] && data[i + 1] > data[i + 2]) return true;
	}
	return false;
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

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
	reqs.physical_device_supported = vertex_input_dynamic_state_supported;
	reqs.device_extensions.push_back(VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
	VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT requested_features = {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT, nullptr, VK_TRUE};
	reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&requested_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_dynamic_vertex_state", reqs);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetVertexInputEXT);

	VkShaderModuleCreateInfo vertex_shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	vertex_shader_info.codeSize = vulkan_dynamic_vertex_state_vert_spirv_len;
	vertex_shader_info.pCode = reinterpret_cast<const uint32_t *>(vulkan_dynamic_vertex_state_vert_spirv);
	VkShaderModule vertex_shader = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &vertex_shader_info, nullptr, &vertex_shader);
	check(result);
	assert(vertex_shader != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)vertex_shader,
	              "dynamic_vertex_state_vertex_shader");

	VkShaderModuleCreateInfo fragment_shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	fragment_shader_info.codeSize = vulkan_dynamic_vertex_state_frag_spirv_len;
	fragment_shader_info.pCode = reinterpret_cast<const uint32_t *>(vulkan_dynamic_vertex_state_frag_spirv);
	VkShaderModule fragment_shader = VK_NULL_HANDLE;
	result = vkCreateShaderModule(vulkan.device, &fragment_shader_info, nullptr, &fragment_shader);
	check(result);
	assert(fragment_shader != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)fragment_shader,
	              "dynamic_vertex_state_fragment_shader");

	VkBuffer vertex_buffer = VK_NULL_HANDLE;
	VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
	set_up_memory(vulkan.device, &vertex_buffer, &vertex_memory);

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)vertex_buffer, "dynamic_vertex_state_vertex_buffer");

	VkDeviceSize vertex_offset = 0;

	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory image_memory = VK_NULL_HANDLE;
	VkImageView image_view = VK_NULL_HANDLE;
	create_image(vulkan, &image, &image_memory, &image_view);
	VkBuffer readback_buffer = VK_NULL_HANDLE;
	VkDeviceMemory readback_memory = VK_NULL_HANDLE;
	create_readback_buffer(vulkan, &readback_buffer, &readback_memory);
	VkRenderPass render_pass = create_render_pass(vulkan);
	VkFramebuffer framebuffer = create_framebuffer(vulkan, render_pass, image_view);
	VkPipelineLayout pipeline_layout = create_pipeline_layout(vulkan);
	VkPipeline pipeline = create_pipeline(vulkan, render_pass, pipeline_layout, vertex_shader, fragment_shader);

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = set_up_command_buffer(vulkan, &command_buffer, &command_pool);
	check(result);

	bench_start_iteration(vulkan.bench);
	VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkClearValue clear_value{};
	clear_value.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
	VkRenderPassBeginInfo render_pass_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr};
	render_pass_begin.renderPass = render_pass;
	render_pass_begin.framebuffer = framebuffer;
	render_pass_begin.renderArea.extent = {target_width, target_height};
	render_pass_begin.clearValueCount = 1;
	render_pass_begin.pClearValues = &clear_value;
	vkCmdBeginRenderPass(command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vertex_offset);
	VkVertexInputBindingDescription2EXT binding_description{};
	binding_description.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
	binding_description.pNext = nullptr;
	binding_description.binding = 0;
	binding_description.stride = sizeof(float) * 2;
	binding_description.divisor = 1;
	binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription2EXT attribute_description{};
	attribute_description.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
	attribute_description.pNext = nullptr;
	attribute_description.location = 0;
	attribute_description.binding = 0;
	attribute_description.format = VK_FORMAT_R32G32_SFLOAT;
	attribute_description.offset = 0;

	pf_vkCmdSetVertexInputEXT(command_buffer, 1, &binding_description, 1, &attribute_description);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	vkCmdEndRenderPass(command_buffer);

	VkBufferImageCopy copy_region{};
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.mipLevel = 0;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent = {target_width, target_height, 1};
	vkCmdCopyImageToBuffer(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                       readback_buffer, 1, &copy_region);
	result = vkEndCommandBuffer(command_buffer);
	check(result);
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);

	result = vkQueueWaitIdle(queue);
	check(result);
	test_marker_mention(vulkan, "Rendered with VK_EXT_vertex_input_dynamic_state", VK_OBJECT_TYPE_PIPELINE,
	                    (uint64_t)pipeline);

	if (vulkan.vkAssertBuffer && get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		const VkUpdateBufferInfoARM assert_info{
		    VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, readback_buffer, 0, target_size, nullptr};
		uint32_t checksum = 0;
		result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum,
		                               "dynamic vertex state color readback");
		check(result);
	}

	void *readback = nullptr;
	result = vkMapMemory(vulkan.device, readback_memory, 0, target_size, 0, &readback);
	check(result);
	assert(readback != nullptr);
	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		assert(image_has_green(reinterpret_cast<const uint8_t *>(readback)));
	}
	vkUnmapMemory(vulkan.device, readback_memory);

	bench_stop_iteration(vulkan.bench);
	vkDestroyBuffer(vulkan.device, vertex_buffer, nullptr);
	testFreeMemory(vulkan, vertex_memory);
	vkDestroyBuffer(vulkan.device, readback_buffer, nullptr);
	testFreeMemory(vulkan, readback_memory);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyFramebuffer(vulkan.device, framebuffer, nullptr);
	vkDestroyRenderPass(vulkan.device, render_pass, nullptr);
	vkDestroyImageView(vulkan.device, image_view, nullptr);
	vkDestroyImage(vulkan.device, image, nullptr);
	testFreeMemory(vulkan, image_memory);
	vkDestroyShaderModule(vulkan.device, fragment_shader, nullptr);
	vkDestroyShaderModule(vulkan.device, vertex_shader, nullptr);
	test_done(vulkan);
	return 0;
}
