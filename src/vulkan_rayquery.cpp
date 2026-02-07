#include "vulkan_common.h"
#include "vulkan_raytracing_common.h"

#include <glm/glm.hpp>

// Shaders from external/vulkan-demos/shaders/glsl/rayquery
// glslangValidator -V scene.vert -o scene.vert.spv
// xxd -i -n vulkan_rayquery_vert_spv scene.vert.spv > vulkan_rayquery.vert.inc
#include "vulkan_rayquery.vert.inc"
// glslangValidator -V scene.frag -o scene.frag.spv
// xxd -i -n vulkan_rayquery_frag_spv scene.frag.spv > vulkan_rayquery.frag.inc
#include "vulkan_rayquery.frag.inc"

struct Vertex
{
	float pos[3];
	float uv[2];
	float color[3];
	float normal[3];
};

struct CameraData
{
	alignas(16) glm::mat4 projection;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 model;
	alignas(16) glm::vec3 lightPos;
};

struct Resources
{
	ray_tracing_common::Context context;
	ray_tracing_common::SimpleAS accel;

	VkImage color_image{VK_NULL_HANDLE};
	VkDeviceMemory color_memory{VK_NULL_HANDLE};
	VkImageView color_view{VK_NULL_HANDLE};
	VkFormat color_format{VK_FORMAT_R8G8B8A8_UNORM};

	VkRenderPass render_pass{VK_NULL_HANDLE};
	VkFramebuffer framebuffer{VK_NULL_HANDLE};

	acceleration_structures::Buffer ubo;
	acceleration_structures::Buffer vertex_buffer;
	acceleration_structures::Buffer index_buffer;

	VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
	VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};
	VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
	VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
	VkPipeline pipeline{VK_NULL_HANDLE};

	VkPipelineShaderStageCreateInfo shader_stages[2]{};
};

static void show_usage()
{
	printf("Minimal ray query graphics test based on rayquery\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static void create_color_target(const vulkan_setup_t& vulkan, Resources& resources, uint32_t width, uint32_t height)
{
	VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = resources.color_format;
	image_info.extent.width = width;
	image_info.extent.height = height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	check(vkCreateImage(vulkan.device, &image_info, nullptr, &resources.color_image));

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(vulkan.device, resources.color_image, &mem_reqs);

	VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	check(vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &resources.color_memory));
	check(vkBindImageMemory(vulkan.device, resources.color_image, resources.color_memory, 0));

	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = resources.color_format;
	view_info.image = resources.color_image;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	check(vkCreateImageView(vulkan.device, &view_info, nullptr, &resources.color_view));
}

static void create_render_pass(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkAttachmentDescription color_attachment{};
	color_attachment.format = resources.color_format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_ref{};
	color_ref.attachment = 0;
	color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;

	VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr};
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	check(vkCreateRenderPass(vulkan.device, &render_pass_info, nullptr, &resources.render_pass));
}

static void create_framebuffer(const vulkan_setup_t& vulkan, Resources& resources, uint32_t width, uint32_t height)
{
	VkImageView attachments[] = { resources.color_view };

	VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr};
	framebuffer_info.renderPass = resources.render_pass;
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = attachments;
	framebuffer_info.width = width;
	framebuffer_info.height = height;
	framebuffer_info.layers = 1;
	check(vkCreateFramebuffer(vulkan.device, &framebuffer_info, nullptr, &resources.framebuffer));
}

static void create_geometry(const vulkan_setup_t& vulkan, Resources& resources)
{
	const std::vector<Vertex> vertices = {
		{{1.0f, 1.0f, 0.0f},  {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
		{{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
		{{0.0f, -1.0f, 0.0f}, {0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
	};

	const std::vector<uint32_t> indices = {0, 1, 2};

	resources.vertex_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		vertices.size() * sizeof(Vertex),
		const_cast<Vertex*>(vertices.data()),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	resources.index_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		indices.size() * sizeof(uint32_t),
		const_cast<uint32_t*>(indices.data()),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

static void create_ubo(const vulkan_setup_t& vulkan, Resources& resources)
{
	CameraData camera{};
	camera.projection = glm::mat4(1.0f);
	camera.view = glm::mat4(1.0f);
	camera.model = glm::mat4(1.0f);
	camera.lightPos = glm::vec3(0.0f, 2.0f, 2.0f);

	resources.ubo = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(CameraData),
		&camera,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

static void create_descriptor_set(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkDescriptorSetLayoutBinding bindings[2]{};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	layout_info.bindingCount = 2;
	layout_info.pBindings = bindings;
	check(vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &resources.descriptor_set_layout));

	VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &resources.descriptor_set_layout;
	check(vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &resources.pipeline_layout));

	VkDescriptorPoolSize pool_sizes[2]{};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[0].descriptorCount = 1;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	pool_sizes[1].descriptorCount = 1;

	VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr};
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 2;
	pool_info.pPoolSizes = pool_sizes;
	check(vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &resources.descriptor_pool));

	VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr};
	alloc_info.descriptorPool = resources.descriptor_pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &resources.descriptor_set_layout;
	check(vkAllocateDescriptorSets(vulkan.device, &alloc_info, &resources.descriptor_set));

	VkDescriptorBufferInfo buffer_info{};
	buffer_info.buffer = resources.ubo.handle;
	buffer_info.offset = 0;
	buffer_info.range = sizeof(CameraData);

	VkWriteDescriptorSetAccelerationStructureKHR as_info{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, nullptr};
	as_info.accelerationStructureCount = 1;
	as_info.pAccelerationStructures = &resources.accel.tlas.handle;

	VkWriteDescriptorSet writes[2]{};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = resources.descriptor_set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].pBufferInfo = &buffer_info;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].pNext = &as_info;
	writes[1].dstSet = resources.descriptor_set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	vkUpdateDescriptorSets(vulkan.device, 2, writes, 0, nullptr);
}

static void create_pipeline(const vulkan_setup_t& vulkan, Resources& resources)
{
	resources.shader_stages[0] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_rayquery_vert_spv, vulkan_rayquery_vert_spv_len, VK_SHADER_STAGE_VERTEX_BIT);
	resources.shader_stages[1] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_rayquery_frag_spv, vulkan_rayquery_frag_spv_len, VK_SHADER_STAGE_FRAGMENT_BIT);

	VkVertexInputBindingDescription binding{};
	binding.binding = 0;
	binding.stride = sizeof(Vertex);
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attributes[4]{};
	attributes[0].location = 0;
	attributes[0].binding = 0;
	attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributes[0].offset = offsetof(Vertex, pos);

	attributes[1].location = 1;
	attributes[1].binding = 0;
	attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
	attributes[1].offset = offsetof(Vertex, uv);

	attributes[2].location = 2;
	attributes[2].binding = 0;
	attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributes[2].offset = offsetof(Vertex, color);

	attributes[3].location = 3;
	attributes[3].binding = 0;
	attributes[3].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributes[3].offset = offsetof(Vertex, normal);

	VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr};
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &binding;
	vertex_input.vertexAttributeDescriptionCount = 4;
	vertex_input.pVertexAttributeDescriptions = attributes;

	VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = 1.0f;
	viewport.height = 1.0f;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.extent.width = 1;
	scissor.extent.height = 1;

	VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr};
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr};
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState color_blend_attachment{};
	color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr};
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &color_blend_attachment;

	VkPipelineDepthStencilStateCreateInfo depth_stencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr};

	VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr};
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = resources.shader_stages;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pDepthStencilState = &depth_stencil;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.layout = resources.pipeline_layout;
	pipeline_info.renderPass = resources.render_pass;
	pipeline_info.subpass = 0;
	check(vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &resources.pipeline));
}

static void draw(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(resources.context.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.context.command_buffer, &begin_info));

	VkClearValue clear_value{};
	clear_value.color.float32[0] = 0.0f;
	clear_value.color.float32[1] = 0.0f;
	clear_value.color.float32[2] = 0.0f;
	clear_value.color.float32[3] = 1.0f;

	VkRenderPassBeginInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr};
	render_pass_info.renderPass = resources.render_pass;
	render_pass_info.framebuffer = resources.framebuffer;
	render_pass_info.renderArea.extent.width = 1;
	render_pass_info.renderArea.extent.height = 1;
	render_pass_info.clearValueCount = 1;
	render_pass_info.pClearValues = &clear_value;

	vkCmdBeginRenderPass(resources.context.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(resources.context.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipeline);
	vkCmdBindDescriptorSets(resources.context.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipeline_layout, 0, 1, &resources.descriptor_set, 0, nullptr);

	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(resources.context.command_buffer, 0, 1, &resources.vertex_buffer.handle, offsets);
	vkCmdBindIndexBuffer(resources.context.command_buffer, resources.index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(resources.context.command_buffer, 3, 1, 0, 0, 0);

	vkCmdEndRenderPass(resources.context.command_buffer);

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
	vkDestroyDescriptorSetLayout(vulkan.device, resources.descriptor_set_layout, nullptr);
	vkDestroyDescriptorPool(vulkan.device, resources.descriptor_pool, nullptr);

	for (auto& stage : resources.shader_stages)
	{
		if (stage.module != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(vulkan.device, stage.module, nullptr);
			stage.module = VK_NULL_HANDLE;
		}
	}

	vkDestroyFramebuffer(vulkan.device, resources.framebuffer, nullptr);
	vkDestroyRenderPass(vulkan.device, resources.render_pass, nullptr);
	vkDestroyImageView(vulkan.device, resources.color_view, nullptr);
	vkDestroyImage(vulkan.device, resources.color_image, nullptr);
	vkFreeMemory(vulkan.device, resources.color_memory, nullptr);

	vkFreeMemory(vulkan.device, resources.ubo.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.ubo.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.vertex_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.vertex_buffer.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.index_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.index_buffer.handle, nullptr);

	ray_tracing_common::destroy_simple_triangle_as(vulkan, resources.context, resources.accel);
	ray_tracing_common::destroy_context(vulkan, resources.context);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray_query_features};

	ray_query_features.rayQuery = VK_TRUE;
	acceleration_features.accelerationStructure = VK_TRUE;

	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.descriptorIndexing = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&acceleration_features);
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.queues = 1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_rayquery", reqs);
	assert(vulkan.hasfeat12.bufferDeviceAddress && "Buffer device address required");

	VkPhysicalDeviceRayQueryFeaturesKHR rq_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq_supported};
	VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &as_supported};
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &supported);
	assert(rq_supported.rayQuery && "Ray query feature required");
	assert(as_supported.accelerationStructure && "Acceleration structure feature required");

	Resources resources{};
	ray_tracing_common::init_context(vulkan, resources.context);
	ray_tracing_common::build_simple_triangle_as(vulkan, resources.context, resources.accel);

	create_color_target(vulkan, resources, 1, 1);
	create_render_pass(vulkan, resources);
	create_framebuffer(vulkan, resources, 1, 1);
	create_geometry(vulkan, resources);
	create_ubo(vulkan, resources);
	create_descriptor_set(vulkan, resources);
	create_pipeline(vulkan, resources);
	draw(vulkan, resources);

	cleanup(vulkan, resources);
	test_done(vulkan);
	return 0;
}
