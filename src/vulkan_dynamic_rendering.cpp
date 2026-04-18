#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

#include "vulkan_dynamic_rendering_frag.inc"
#include "vulkan_dynamic_rendering_vert.inc"

#include <memory>

using namespace tracetooltests;

static void show_usage()
{
	usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return parseCmdopt(i, argc, argv, reqs);
}

struct OffscreenTarget
{
	std::shared_ptr<Image> image;
	std::shared_ptr<ImageView> view;
};

static OffscreenTarget create_color_target(const vulkan_setup_t& vulkan, VkExtent2D extent)
{
	OffscreenTarget target{};
	uint32_t queue_family_index = 0;

	target.image = std::make_shared<Image>(vulkan.device);
	target.image->create({ extent.width, extent.height, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
	                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, &queue_family_index);

	target.view = std::make_shared<ImageView>(target.image);
	target.view->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	return target;
}

static VkPipeline create_pipeline(const vulkan_setup_t& vulkan, VkPipelineLayout layout, VkFormat color_format)
{
	auto vert = std::make_shared<Shader>(vulkan.device);
	vert->create(vulkan_dynamic_rendering_vert_spv, vulkan_dynamic_rendering_vert_spv_len);

	auto frag = std::make_shared<Shader>(vulkan.device);
	frag->create(vulkan_dynamic_rendering_frag_spv, vulkan_dynamic_rendering_frag_spv_len);

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert->getHandle();
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag->getHandle();
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr };
	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr
	};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr };
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;

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

	VkPipelineColorBlendAttachmentState color_attachment = {};
	color_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo color_blend = {
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr
	};
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &color_attachment;

	VkDynamicState dynamic_states[2] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamic_state = {
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr
	};
	dynamic_state.dynamicStateCount = 2;
	dynamic_state.pDynamicStates = dynamic_states;

	VkPipelineRenderingCreateInfoKHR rendering = {
		VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR, nullptr, 0, 1, &color_format, VK_FORMAT_UNDEFINED,
		VK_FORMAT_UNDEFINED
	};

	VkGraphicsPipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering };
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = stages;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterization;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = layout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(result);
	return pipeline;
}

static void record_secondary(const std::shared_ptr<CommandBuffer>& command_buffer, VkFormat color_format, VkExtent2D extent, VkPipeline pipeline)
{
	VkCommandBufferInheritanceRenderingInfoKHR inheritance_rendering = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO_KHR, nullptr, 0, 0, 1, &color_format, VK_FORMAT_UNDEFINED,
		VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT
	};
	VkCommandBufferInheritanceInfo inheritance = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, &inheritance_rendering
	};
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
	begin_info.pInheritanceInfo = &inheritance;

	VkResult result = vkBeginCommandBuffer(command_buffer->getHandle(), &begin_info);
	check(result);

	vkCmdBindPipeline(command_buffer->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkViewport viewport = { 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f };
	vkCmdSetViewport(command_buffer->getHandle(), 0, 1, &viewport);

	VkRect2D scissor = { { 0, 0 }, extent };
	vkCmdSetScissor(command_buffer->getHandle(), 0, 1, &scissor);

	vkCmdDraw(command_buffer->getHandle(), 3, 1, 0, 0);

	result = command_buffer->end();
	check(result);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR, nullptr, VK_TRUE
	};
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.maxApiVersion = VK_API_VERSION_1_2;
	reqs.device_extensions.push_back("VK_KHR_dynamic_rendering");
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&dynamic_rendering);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_dynamic_rendering", reqs);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBeginRenderingKHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdEndRenderingKHR);

	{
		VkQueue queue = VK_NULL_HANDLE;
		vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

		VkExtent2D extent = { 64, 64 };
		OffscreenTarget color_target = create_color_target(vulkan, extent);

		auto pipeline_layout = std::make_shared<PipelineLayout>(vulkan.device);
		pipeline_layout->create(std::vector<VkPushConstantRange> {});

		VkPipeline pipeline = create_pipeline(vulkan, pipeline_layout->getHandle(), color_target.image->m_format);

		auto command_pool = std::make_shared<CommandBufferPool>(vulkan.device);
		command_pool->create(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0);

		auto primary = std::make_shared<CommandBuffer>(command_pool);
		primary->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

		auto secondary = std::make_shared<CommandBuffer>(command_pool);
		secondary->create(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
		record_secondary(secondary, color_target.image->m_format, extent, pipeline);

		VkResult result = primary->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		check(result);

		primary->imageMemoryBarrier(*color_target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                            0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		VkRenderingAttachmentInfoKHR color_attachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR, nullptr };
		color_attachment.imageView = color_target.view->getHandle();
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

		VkRenderingInfoKHR rendering = { VK_STRUCTURE_TYPE_RENDERING_INFO_KHR, nullptr };
		rendering.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT_KHR;
		rendering.renderArea.extent = extent;
		rendering.layerCount = 1;
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments = &color_attachment;

		pf_vkCmdBeginRenderingKHR(primary->getHandle(), &rendering);
		VkCommandBuffer secondary_handle = secondary->getHandle();
		vkCmdExecuteCommands(primary->getHandle(), 1, &secondary_handle);
		pf_vkCmdEndRenderingKHR(primary->getHandle());

		result = primary->end();
		check(result);

		bench_start_iteration(vulkan.bench);
		VkCommandBuffer primary_handle = primary->getHandle();
		VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &primary_handle;
		result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
		check(result);
		result = vkQueueWaitIdle(queue);
		check(result);
		bench_stop_iteration(vulkan.bench);

		vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	}

	test_done(vulkan);
	return 0;
}
