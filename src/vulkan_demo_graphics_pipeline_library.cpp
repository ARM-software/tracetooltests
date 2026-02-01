// Minimal graphics pipeline library test using VK_EXT_graphics_pipeline_library.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our shaders, generated with:
//   xxd -i -n vulkan_demo_graphics_pipeline_library_shared_vert_spv content/vulkan-demos/shaders/glsl/graphicspipelinelibrary/shared.vert.spv > src/vulkan_demo_graphics_pipeline_library_shared_vert.inc
//   xxd -i -n vulkan_demo_graphics_pipeline_library_uber_frag_spv content/vulkan-demos/shaders/glsl/graphicspipelinelibrary/uber.frag.spv > src/vulkan_demo_graphics_pipeline_library_uber_frag.inc
#include "vulkan_demo_graphics_pipeline_library_shared_vert.inc"
#include "vulkan_demo_graphics_pipeline_library_uber_frag.inc"

#include <glm/glm.hpp>

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace tracetooltests;

static void show_usage()
{
	usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return parseCmdopt(i, argc, argv, reqs);
}

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec3 color;
};

struct UniformData
{
	glm::mat4 projection;
	glm::mat4 model;
	glm::vec4 lightPos;
};

struct ColorTarget
{
	VkExtent2D extent{};
	std::shared_ptr<Image> image;
	std::shared_ptr<ImageView> view;
	std::shared_ptr<RenderPass> renderPass;
	std::shared_ptr<FrameBuffer> framebuffer;
};

static ColorTarget create_color_target(const vulkan_setup_t& vulkan, VkExtent2D extent)
{
	ColorTarget target{};
	target.extent = extent;

	target.image = std::make_shared<Image>(vulkan.device);
	target.image->create({extent.width, extent.height, 1}, VK_FORMAT_R8G8B8A8_UNORM,
	                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	target.view = std::make_shared<ImageView>(target.image);
	target.view->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	AttachmentInfo color{0, target.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	color.m_description.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.m_description.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	SubpassInfo subpass{};
	subpass.addColorAttachment(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	target.renderPass = std::make_shared<RenderPass>(vulkan.device);
	target.renderPass->create({color}, {subpass});

	target.framebuffer = std::make_shared<FrameBuffer>(vulkan.device);
	target.framebuffer->create(*target.renderPass, extent);

	return target;
}

static void transition_to_color(CommandBuffer& cmd, Image& image)
{
	if (image.m_imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		return;

	cmd.imageMemoryBarrier(image, image.m_imageLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                       0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

static void set_viewport_scissor(VkCommandBuffer cmd, VkExtent2D extent)
{
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = {0, 0};
	scissor.extent = extent;
	vkCmdSetScissor(cmd, 0, 1, &scissor);
}

class DemoGraphicsPipelineLibraryContext : public GraphicContext
{
public:
	DemoGraphicsPipelineLibraryContext() : GraphicContext() {}
	~DemoGraphicsPipelineLibraryContext() {
		destroy();
	}

	void destroy()
	{
		colorTarget = {};

		vertexBuffer = nullptr;
		uniformBuffer = nullptr;
		descriptor = nullptr;

		if (pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_vulkanSetup.device, pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}
		if (fragmentShaderLibrary != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_vulkanSetup.device, fragmentShaderLibrary, nullptr);
			fragmentShaderLibrary = VK_NULL_HANDLE;
		}
		if (fragmentOutputLibrary != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_vulkanSetup.device, fragmentOutputLibrary, nullptr);
			fragmentOutputLibrary = VK_NULL_HANDLE;
		}
		if (preRasterLibrary != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_vulkanSetup.device, preRasterLibrary, nullptr);
			preRasterLibrary = VK_NULL_HANDLE;
		}
		if (vertexInputLibrary != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_vulkanSetup.device, vertexInputLibrary, nullptr);
			vertexInputLibrary = VK_NULL_HANDLE;
		}

		pipelineLayout = nullptr;

		if (frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, frameFence, nullptr);
			frameFence = VK_NULL_HANDLE;
		}
	}

	ColorTarget colorTarget;
	std::unique_ptr<Buffer> vertexBuffer;
	std::unique_ptr<Buffer> uniformBuffer;
	std::unique_ptr<DescriptorSet> descriptor;
	std::shared_ptr<PipelineLayout> pipelineLayout;

	VkPipeline vertexInputLibrary = VK_NULL_HANDLE;
	VkPipeline preRasterLibrary = VK_NULL_HANDLE;
	VkPipeline fragmentOutputLibrary = VK_NULL_HANDLE;
	VkPipeline fragmentShaderLibrary = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;

	uint32_t vertexCount = 0;
	VkFence frameFence = VK_NULL_HANDLE;
};

static std::unique_ptr<DemoGraphicsPipelineLibraryContext> p_benchmark = nullptr;

static void render(const vulkan_setup_t& vulkan)
{
	bool first_loop = true;
	benchmarking bench = vulkan.bench;

	while (p__loops--)
	{
		VkCommandBuffer cmd = p_benchmark->m_defaultCommandBuffer->getHandle();

		VkResult result = vkWaitForFences(vulkan.device, 1, &p_benchmark->frameFence, VK_TRUE, UINT64_MAX);
		check(result);

		if (!first_loop)
		{
			bench_stop_iteration(bench);
		}

		result = vkResetFences(vulkan.device, 1, &p_benchmark->frameFence);
		check(result);
		result = vkResetCommandBuffer(cmd, 0);
		check(result);

		p_benchmark->m_defaultCommandBuffer->begin();
		transition_to_color(*p_benchmark->m_defaultCommandBuffer, *p_benchmark->colorTarget.image);
		p_benchmark->m_defaultCommandBuffer->beginRenderPass(*p_benchmark->colorTarget.renderPass, *p_benchmark->colorTarget.framebuffer);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->pipeline);
		set_viewport_scissor(cmd, p_benchmark->colorTarget.extent);

		VkDescriptorSet descriptor_set = p_benchmark->descriptor->getHandle();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->pipelineLayout->getHandle(),
		                        0, 1, &descriptor_set, 0, nullptr);

		VkBuffer vertex_buffers[] = {p_benchmark->vertexBuffer->getHandle()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
		vkCmdDraw(cmd, p_benchmark->vertexCount, 1, 0, 0);

		p_benchmark->m_defaultCommandBuffer->endRenderPass();
		p_benchmark->m_defaultCommandBuffer->end();

		bench_start_iteration(bench);
		p_benchmark->submit(p_benchmark->m_defaultQueue,
		                    std::vector<std::shared_ptr<CommandBuffer>> {p_benchmark->m_defaultCommandBuffer},
		                    p_benchmark->frameFence);

		first_loop = false;
	}

	VkResult result = vkWaitForFences(vulkan.device, 1, &p_benchmark->frameFence, VK_TRUE, UINT64_MAX);
	check(result);
	bench_stop_iteration(bench);
}

int main(int argc, char** argv)
{
	p_benchmark = std::make_unique<DemoGraphicsPipelineLibraryContext>();

	VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT gpl_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT, nullptr};
	gpl_features.graphicsPipelineLibrary = VK_TRUE;

	vulkan_req_t req{};
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.apiVersion = VK_API_VERSION_1_1;
	req.minApiVersion = VK_API_VERSION_1_1;
	req.device_extensions.push_back("VK_KHR_pipeline_library");
	req.device_extensions.push_back("VK_EXT_graphics_pipeline_library");
	req.extension_features = reinterpret_cast<VkBaseInStructure*>(&gpl_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_demo_graphics_pipeline_library", req);
	p_benchmark->initBasic(vulkan, req);

	p_benchmark->colorTarget = create_color_target(vulkan, {p_benchmark->width, p_benchmark->height});

	const std::array<Vertex, 3> kTriangle = {{
		{{-0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.9f, 0.2f, 0.2f}},
		{{ 0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.2f, 0.9f, 0.2f}},
		{{ 0.0f,  0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.2f, 0.2f, 0.9f}},
	}};

	auto vertexBuffer = std::make_unique<Buffer>(vulkan);
	vertexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                     sizeof(Vertex) * kTriangle.size(), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	std::vector<Vertex> vertex_data(kTriangle.begin(), kTriangle.end());
	p_benchmark->updateBuffer(vertex_data, *vertexBuffer);

	auto uniformBuffer = std::make_unique<Buffer>(vulkan);
	uniformBuffer->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UniformData),
	                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	uniformBuffer->map();
	UniformData uniform_data{};
	uniform_data.projection = glm::mat4(1.0f);
	uniform_data.model = glm::mat4(1.0f);
	uniform_data.lightPos = glm::vec4(0.0f, -2.0f, 1.0f, 0.0f);
	memcpy(uniformBuffer->m_mappedAddress, &uniform_data, sizeof(uniform_data));
	uniformBuffer->flush(true);
	uniformBuffer->unmap();

	p_benchmark->submitStaging(true, {}, {}, false);

	auto descriptorLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	descriptorLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	descriptorLayout->create();

	auto descriptorPool = std::make_shared<DescriptorSetPool>(descriptorLayout);
	descriptorPool->create(1);

	auto descriptor = std::make_unique<DescriptorSet>(descriptorPool);
	descriptor->create();
	descriptor->setBuffer(0, *uniformBuffer);
	descriptor->update();

	{
		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> layout_map = {
			{0, descriptorLayout},
		};
		p_benchmark->pipelineLayout = std::make_shared<PipelineLayout>(vulkan.device);
		p_benchmark->pipelineLayout->create(layout_map);
	}

	{
		VkGraphicsPipelineLibraryCreateInfoEXT library_info{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT, nullptr};
		library_info.flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;

		VkPipelineVertexInputStateCreateInfo vertex_input{
			VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr};
		VkVertexInputBindingDescription binding{};
		binding.binding = 0;
		binding.stride = sizeof(Vertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		std::array<VkVertexInputAttributeDescription, 3> attributes = {{
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
			{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
			{2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
		}};
		vertex_input.vertexBindingDescriptionCount = 1;
		vertex_input.pVertexBindingDescriptions = &binding;
		vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
		vertex_input.pVertexAttributeDescriptions = attributes.data();

		VkPipelineInputAssemblyStateCreateInfo input_assembly{
			VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr};
		input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		input_assembly.primitiveRestartEnable = VK_FALSE;

		VkGraphicsPipelineCreateInfo pipeline_ci{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &library_info};
		pipeline_ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
		pipeline_ci.pVertexInputState = &vertex_input;
		pipeline_ci.pInputAssemblyState = &input_assembly;

		VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
		                                            &p_benchmark->vertexInputLibrary);
		check(result);
	}

	{
		auto vertShader = std::make_shared<Shader>(vulkan.device);
		vertShader->create(vulkan_demo_graphics_pipeline_library_shared_vert_spv,
		                   vulkan_demo_graphics_pipeline_library_shared_vert_spv_len);

		VkPipelineShaderStageCreateInfo shader_stage{
			VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr};
		shader_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
		shader_stage.module = vertShader->getHandle();
		shader_stage.pName = "main";

		VkGraphicsPipelineLibraryCreateInfoEXT library_info{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT, nullptr};
		library_info.flags = VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT;

		std::array<VkDynamicState, 2> dynamics = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamic_info{
			VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr};
		dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamics.size());
		dynamic_info.pDynamicStates = dynamics.data();

		VkPipelineViewportStateCreateInfo viewport_state{
			VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr};
		viewport_state.viewportCount = 1;
		viewport_state.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo raster_state{
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr};
		raster_state.polygonMode = VK_POLYGON_MODE_FILL;
		raster_state.cullMode = VK_CULL_MODE_BACK_BIT;
		raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		raster_state.lineWidth = 1.0f;

		VkGraphicsPipelineCreateInfo pipeline_ci{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &library_info};
		pipeline_ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
		pipeline_ci.stageCount = 1;
		pipeline_ci.pStages = &shader_stage;
		pipeline_ci.layout = p_benchmark->pipelineLayout->getHandle();
		pipeline_ci.renderPass = p_benchmark->colorTarget.renderPass->getHandle();
		pipeline_ci.pDynamicState = &dynamic_info;
		pipeline_ci.pViewportState = &viewport_state;
		pipeline_ci.pRasterizationState = &raster_state;

		VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
		                                            &p_benchmark->preRasterLibrary);
		check(result);
	}

	{
		auto fragShader = std::make_shared<Shader>(vulkan.device);
		fragShader->create(vulkan_demo_graphics_pipeline_library_uber_frag_spv,
		                   vulkan_demo_graphics_pipeline_library_uber_frag_spv_len);

		VkPipelineShaderStageCreateInfo shader_stage{
			VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr};
		shader_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shader_stage.module = fragShader->getHandle();
		shader_stage.pName = "main";
		uint32_t lighting_model = 0;
		VkSpecializationMapEntry spec_entry{};
		spec_entry.constantID = 0;
		spec_entry.offset = 0;
		spec_entry.size = sizeof(uint32_t);
		VkSpecializationInfo spec_info{};
		spec_info.mapEntryCount = 1;
		spec_info.pMapEntries = &spec_entry;
		spec_info.dataSize = sizeof(uint32_t);
		spec_info.pData = &lighting_model;
		shader_stage.pSpecializationInfo = &spec_info;

		VkGraphicsPipelineLibraryCreateInfoEXT library_info{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT, nullptr};
		library_info.flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;

		VkPipelineDepthStencilStateCreateInfo depth_state{
			VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr};
		depth_state.depthTestEnable = VK_FALSE;
		depth_state.depthWriteEnable = VK_FALSE;
		depth_state.depthCompareOp = VK_COMPARE_OP_ALWAYS;

		VkPipelineMultisampleStateCreateInfo ms_state{
			VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr};
		ms_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkGraphicsPipelineCreateInfo pipeline_ci{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &library_info};
		pipeline_ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
		pipeline_ci.stageCount = 1;
		pipeline_ci.pStages = &shader_stage;
		pipeline_ci.layout = p_benchmark->pipelineLayout->getHandle();
		pipeline_ci.renderPass = p_benchmark->colorTarget.renderPass->getHandle();
		pipeline_ci.pDepthStencilState = &depth_state;
		pipeline_ci.pMultisampleState = &ms_state;

		VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
		                                            &p_benchmark->fragmentShaderLibrary);
		check(result);
	}

	{
		VkGraphicsPipelineLibraryCreateInfoEXT library_info{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT, nullptr};
		library_info.flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;

		VkPipelineColorBlendAttachmentState blend{};
		blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blend.blendEnable = VK_FALSE;
		VkPipelineColorBlendStateCreateInfo blend_state{
			VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr};
		blend_state.attachmentCount = 1;
		blend_state.pAttachments = &blend;

		VkPipelineMultisampleStateCreateInfo ms_state{
			VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr};
		ms_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkGraphicsPipelineCreateInfo pipeline_ci{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &library_info};
		pipeline_ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
		pipeline_ci.layout = p_benchmark->pipelineLayout->getHandle();
		pipeline_ci.renderPass = p_benchmark->colorTarget.renderPass->getHandle();
		pipeline_ci.pColorBlendState = &blend_state;
		pipeline_ci.pMultisampleState = &ms_state;

		VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
		                                            &p_benchmark->fragmentOutputLibrary);
		check(result);
	}

	{
		std::array<VkPipeline, 4> libraries = {
			p_benchmark->vertexInputLibrary,
			p_benchmark->preRasterLibrary,
			p_benchmark->fragmentShaderLibrary,
			p_benchmark->fragmentOutputLibrary,
		};
		VkPipelineLibraryCreateInfoKHR library_ci{
			VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR, nullptr};
		library_ci.libraryCount = static_cast<uint32_t>(libraries.size());
		library_ci.pLibraries = libraries.data();

		VkGraphicsPipelineCreateInfo pipeline_ci{
			VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &library_ci};
		pipeline_ci.layout = p_benchmark->pipelineLayout->getHandle();
		VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
		                                            &p_benchmark->pipeline);
		check(result);
	}

	p_benchmark->vertexBuffer = std::move(vertexBuffer);
	p_benchmark->uniformBuffer = std::move(uniformBuffer);
	p_benchmark->descriptor = std::move(descriptor);
	p_benchmark->vertexCount = static_cast<uint32_t>(kTriangle.size());

	descriptorLayout = nullptr;
	descriptorPool = nullptr;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VkResult result = vkCreateFence(vulkan.device, &fenceInfo, nullptr, &p_benchmark->frameFence);
	check(result);

	render(vulkan);

	result = vkDeviceWaitIdle(vulkan.device);
	check(result);

	p_benchmark = nullptr;
	return 0;
}
