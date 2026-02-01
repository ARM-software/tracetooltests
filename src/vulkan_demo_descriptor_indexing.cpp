// Minimal graphics test using VK_EXT_descriptor_indexing.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our shaders, generated with:
//   xxd -i -n vulkan_demo_descriptor_indexing_vert_spv content/vulkan-demos/shaders/glsl/descriptorindexing/descriptorindexing.vert.spv > src/vulkan_demo_descriptor_indexing_vert.inc
//   xxd -i -n vulkan_demo_descriptor_indexing_frag_spv content/vulkan-demos/shaders/glsl/descriptorindexing/descriptorindexing.frag.spv > src/vulkan_demo_descriptor_indexing_frag.inc
#include "vulkan_demo_descriptor_indexing_vert.inc"
#include "vulkan_demo_descriptor_indexing_frag.inc"

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
	glm::vec2 uv;
	int32_t texIndex;
};

struct UniformData
{
	glm::mat4 projection;
	glm::mat4 view;
	glm::mat4 model;
};

struct ColorTarget
{
	VkExtent2D extent{};
	std::shared_ptr<Image> image;
	std::shared_ptr<ImageView> view;
	std::shared_ptr<RenderPass> renderPass;
	std::shared_ptr<FrameBuffer> framebuffer;
};

static void append_quad(std::vector<Vertex>& verts, float cx, float cy, float half, int32_t texIndex)
{
	float x0 = cx - half;
	float x1 = cx + half;
	float y0 = cy - half;
	float y1 = cy + half;
	verts.push_back({{x0, y0, 0.0f}, {0.0f, 0.0f}, texIndex});
	verts.push_back({{x1, y0, 0.0f}, {1.0f, 0.0f}, texIndex});
	verts.push_back({{x1, y1, 0.0f}, {1.0f, 1.0f}, texIndex});
	verts.push_back({{x0, y0, 0.0f}, {0.0f, 0.0f}, texIndex});
	verts.push_back({{x1, y1, 0.0f}, {1.0f, 1.0f}, texIndex});
	verts.push_back({{x0, y1, 0.0f}, {0.0f, 1.0f}, texIndex});
}

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

class DemoDescriptorIndexingContext : public GraphicContext
{
public:
	DemoDescriptorIndexingContext() : GraphicContext() {}
	~DemoDescriptorIndexingContext() {
		destroy();
	}

	void destroy()
	{
		colorTarget = {};

		vertexBuffer = nullptr;
		uniformBuffer = nullptr;

		descriptor = nullptr;
		pipeline = nullptr;
		pipelineLayout = nullptr;

		sampler = nullptr;
		textureViews.clear();
		textureImages.clear();

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
	std::unique_ptr<GraphicPipeline> pipeline;
	std::unique_ptr<Sampler> sampler;
	std::vector<std::shared_ptr<Image>> textureImages;
	std::vector<std::shared_ptr<ImageView>> textureViews;
	uint32_t vertexCount = 0;
	VkFence frameFence = VK_NULL_HANDLE;
};

static std::unique_ptr<DemoDescriptorIndexingContext> p_benchmark = nullptr;

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

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->pipeline->getHandle());
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
	p_benchmark = std::make_unique<DemoDescriptorIndexingContext>();

	VkPhysicalDeviceDescriptorIndexingFeaturesEXT desc_index_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT, nullptr};
	desc_index_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	desc_index_features.runtimeDescriptorArray = VK_TRUE;
	desc_index_features.descriptorBindingVariableDescriptorCount = VK_TRUE;

	vulkan_req_t req{};
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.apiVersion = VK_API_VERSION_1_1;
	req.minApiVersion = VK_API_VERSION_1_1;
	req.device_extensions.push_back("VK_EXT_descriptor_indexing");
	req.extension_features = reinterpret_cast<VkBaseInStructure*>(&desc_index_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_demo_descriptor_indexing", req);
	p_benchmark->initBasic(vulkan, req);

	p_benchmark->colorTarget = create_color_target(vulkan, {p_benchmark->width, p_benchmark->height});

	const std::array<std::array<uint8_t, 4>, 4> kColors = {{
		{{255, 0, 0, 255}},
		{{0, 255, 0, 255}},
		{{0, 0, 255, 255}},
		{{255, 255, 0, 255}},
	}};
	const uint32_t tex_extent = 4;
	const uint32_t texture_count = static_cast<uint32_t>(kColors.size());

	p_benchmark->textureImages.reserve(texture_count);
	p_benchmark->textureViews.reserve(texture_count);

	for (uint32_t i = 0; i < texture_count; ++i)
	{
		std::vector<uint8_t> tex_data(tex_extent * tex_extent * 4);
		for (size_t t = 0; t < tex_extent * tex_extent; ++t)
		{
			tex_data[t * 4 + 0] = kColors[i][0];
			tex_data[t * 4 + 1] = kColors[i][1];
			tex_data[t * 4 + 2] = kColors[i][2];
			tex_data[t * 4 + 3] = kColors[i][3];
		}

		auto image = std::make_shared<Image>(vulkan.device);
		image->create({tex_extent, tex_extent, 1}, VK_FORMAT_R8G8B8A8_UNORM,
		              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		auto view = std::make_shared<ImageView>(image);
		view->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
		p_benchmark->updateImage(reinterpret_cast<const char*>(tex_data.data()), tex_data.size(), *image,
		                         {tex_extent, tex_extent, 1});

		p_benchmark->textureImages.push_back(image);
		p_benchmark->textureViews.push_back(view);
	}

	auto sampler = std::make_unique<Sampler>(vulkan.device);
	sampler->create(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
	                VK_FALSE, 1.0f);

	std::vector<Vertex> vertices;
	append_quad(vertices, -0.5f, -0.5f, 0.4f, 0);
	append_quad(vertices,  0.5f, -0.5f, 0.4f, 1);
	append_quad(vertices, -0.5f,  0.5f, 0.4f, 2);
	append_quad(vertices,  0.5f,  0.5f, 0.4f, 3);

	auto vertexBuffer = std::make_unique<Buffer>(vulkan);
	vertexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                     sizeof(Vertex) * vertices.size(), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	p_benchmark->updateBuffer(vertices, *vertexBuffer);

	auto uniformBuffer = std::make_unique<Buffer>(vulkan);
	uniformBuffer->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UniformData),
	                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	uniformBuffer->map();
	UniformData uniform_data{};
	uniform_data.projection = glm::mat4(1.0f);
	uniform_data.view = glm::mat4(1.0f);
	uniform_data.model = glm::mat4(1.0f);
	memcpy(uniformBuffer->m_mappedAddress, &uniform_data, sizeof(uniform_data));
	uniformBuffer->flush(true);
	uniformBuffer->unmap();

	p_benchmark->submitStaging(true, {}, {}, false);

	auto descriptorLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	descriptorLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	descriptorLayout->insertBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, texture_count);
	std::vector<VkDescriptorBindingFlags> binding_flags = {
		0,
		VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT
	};
	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr};
	binding_flags_info.bindingCount = static_cast<uint32_t>(binding_flags.size());
	binding_flags_info.pBindingFlags = binding_flags.data();
	descriptorLayout->insertNext(binding_flags_info);
	descriptorLayout->create(static_cast<uint32_t>(descriptorLayout->getBindings().size()), 0);

	auto descriptorPool = std::make_shared<DescriptorSetPool>(descriptorLayout);
	descriptorPool->create(1);

	auto descriptor = std::make_unique<DescriptorSet>(descriptorPool);
	VkDescriptorSetVariableDescriptorCountAllocateInfo var_count_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT, nullptr};
	uint32_t variable_count = texture_count;
	var_count_info.descriptorSetCount = 1;
	var_count_info.pDescriptorCounts = &variable_count;
	descriptor->insertNext(var_count_info);
	descriptor->create();
	descriptor->setBuffer(0, *uniformBuffer);
	for (auto& view : p_benchmark->textureViews)
	{
		descriptor->setCombinedImageSampler(1, *view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *sampler);
	}
	descriptor->update();

	{
		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> layout_map = {
			{0, descriptorLayout},
		};
		p_benchmark->pipelineLayout = std::make_shared<PipelineLayout>(vulkan.device);
		p_benchmark->pipelineLayout->create(layout_map);
	}

	{
		auto vertShader = std::make_shared<Shader>(vulkan.device);
		vertShader->create(vulkan_demo_descriptor_indexing_vert_spv, vulkan_demo_descriptor_indexing_vert_spv_len);
		auto fragShader = std::make_shared<Shader>(vulkan.device);
		fragShader->create(vulkan_demo_descriptor_indexing_frag_spv, vulkan_demo_descriptor_indexing_frag_spv_len);

		GraphicPipelineState pipelineState;
		pipelineState.setVertexBinding(0, *vertexBuffer, sizeof(Vertex));
		pipelineState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos));
		pipelineState.setVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv));
		pipelineState.setVertexAttribute(2, 0, VK_FORMAT_R32_SINT, offsetof(Vertex, texIndex));
		pipelineState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		pipelineState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		pipelineState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		pipelineState.m_rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		pipelineState.m_rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
		pipelineState.m_rasterizationState.lineWidth = 1.0f;
		pipelineState.m_depthStencilState.depthTestEnable = VK_FALSE;
		pipelineState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		pipelineState.m_depthStencilState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		VkPipelineColorBlendAttachmentState blend{};
		blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blend.blendEnable = VK_FALSE;
		pipelineState.setColorBlendAttachment(0, blend);

		ShaderPipelineState vertStage(VK_SHADER_STAGE_VERTEX_BIT, vertShader);
		ShaderPipelineState fragStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader);

		p_benchmark->pipeline = std::make_unique<GraphicPipeline>(p_benchmark->pipelineLayout);
		p_benchmark->pipeline->create({vertStage, fragStage}, pipelineState, *p_benchmark->colorTarget.renderPass);
	}

	p_benchmark->vertexBuffer = std::move(vertexBuffer);
	p_benchmark->uniformBuffer = std::move(uniformBuffer);
	p_benchmark->descriptor = std::move(descriptor);
	p_benchmark->sampler = std::move(sampler);
	p_benchmark->vertexCount = static_cast<uint32_t>(vertices.size());

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
