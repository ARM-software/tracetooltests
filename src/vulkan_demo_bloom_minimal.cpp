// Minimal bloom-style blur using offscreen passes.
// Based on the bloom demo from Sascha Willem's vulkan examples.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our shaders, generated with:
//   xxd -i -n vulkan_demo_bloom_minimal_colorpass_vert_spv content/vulkan-demos/shaders/glsl/bloom/colorpass.vert.spv > src/vulkan_demo_bloom_minimal_colorpass_vert.inc
//   xxd -i -n vulkan_demo_bloom_minimal_colorpass_frag_spv content/vulkan-demos/shaders/glsl/bloom/colorpass.frag.spv > src/vulkan_demo_bloom_minimal_colorpass_frag.inc
//   xxd -i -n vulkan_demo_bloom_minimal_gaussblur_vert_spv content/vulkan-demos/shaders/glsl/bloom/gaussblur.vert.spv > src/vulkan_demo_bloom_minimal_gaussblur_vert.inc
//   xxd -i -n vulkan_demo_bloom_minimal_gaussblur_frag_spv content/vulkan-demos/shaders/glsl/bloom/gaussblur.frag.spv > src/vulkan_demo_bloom_minimal_gaussblur_frag.inc

#include "vulkan_demo_bloom_minimal_colorpass_vert.inc"
#include "vulkan_demo_bloom_minimal_colorpass_frag.inc"
#include "vulkan_demo_bloom_minimal_gaussblur_vert.inc"
#include "vulkan_demo_bloom_minimal_gaussblur_frag.inc"

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
	glm::vec4 pos;
	glm::vec2 uv;
	glm::vec3 color;
};

static const std::array<Vertex, 6> kFullscreenQuad = {{
	{{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.3f, 0.2f}},
	{{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.2f, 1.0f, 0.3f}},
	{{ 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.2f, 0.3f, 1.0f}},
	{{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.3f, 0.2f}},
	{{ 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.2f, 0.3f, 1.0f}},
	{{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.9f, 0.9f, 0.9f}},
}};

struct SceneUBO
{
	glm::mat4 projection;
	glm::mat4 view;
	glm::mat4 model;
};

struct BlurUBO
{
	glm::vec4 params;
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
	                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

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

	VkAccessFlags src_access = 0;
	VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	if (image.m_imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		src_access = VK_ACCESS_SHADER_READ_BIT;
		src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}

	cmd.imageMemoryBarrier(image, image.m_imageLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                       src_access, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                       src_stage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

static void transition_to_read(CommandBuffer& cmd, Image& image)
{
	if (image.m_imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		return;

	VkAccessFlags src_access = 0;
	VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	if (image.m_imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}

	cmd.imageMemoryBarrier(image, image.m_imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                       src_access, VK_ACCESS_SHADER_READ_BIT,
	                       src_stage, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
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

class BloomContext : public GraphicContext
{
public:
	BloomContext() : GraphicContext() {}
	~BloomContext() {
		destroy();
	}

	void destroy()
	{
		sceneTarget = {};
		blurTargetH = {};
		blurTargetV = {};

		vertexBuffer = nullptr;
		sceneUbo = nullptr;
		blurUbo = nullptr;

		sampler = nullptr;
		dummyImageView = nullptr;
		dummyImage = nullptr;

		sceneDescriptor = nullptr;
		blurDescriptorH = nullptr;
		blurDescriptorV = nullptr;

		scenePipeline = nullptr;
		blurPipelineH = nullptr;
		blurPipelineV = nullptr;

		scenePipelineLayout = nullptr;
		blurPipelineLayout = nullptr;

		if (frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, frameFence, nullptr);
			frameFence = VK_NULL_HANDLE;
		}
	}

	ColorTarget sceneTarget;
	ColorTarget blurTargetH;
	ColorTarget blurTargetV;

	std::unique_ptr<Buffer> vertexBuffer;
	std::unique_ptr<Buffer> sceneUbo;
	std::unique_ptr<Buffer> blurUbo;

	std::unique_ptr<Sampler> sampler;
	std::shared_ptr<Image> dummyImage;
	std::shared_ptr<ImageView> dummyImageView;

	std::unique_ptr<DescriptorSet> sceneDescriptor;
	std::unique_ptr<DescriptorSet> blurDescriptorH;
	std::unique_ptr<DescriptorSet> blurDescriptorV;

	std::shared_ptr<PipelineLayout> scenePipelineLayout;
	std::shared_ptr<PipelineLayout> blurPipelineLayout;

	std::unique_ptr<GraphicPipeline> scenePipeline;
	std::unique_ptr<GraphicPipeline> blurPipelineH;
	std::unique_ptr<GraphicPipeline> blurPipelineV;

	VkFence frameFence = VK_NULL_HANDLE;
};

static std::unique_ptr<BloomContext> p_benchmark = nullptr;

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

		transition_to_color(*p_benchmark->m_defaultCommandBuffer, *p_benchmark->sceneTarget.image);
		p_benchmark->m_defaultCommandBuffer->beginRenderPass(*p_benchmark->sceneTarget.renderPass, *p_benchmark->sceneTarget.framebuffer);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->scenePipeline->getHandle());
		VkDescriptorSet scene_set = p_benchmark->sceneDescriptor->getHandle();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->scenePipelineLayout->getHandle(), 0, 1, &scene_set, 0, nullptr);
		set_viewport_scissor(cmd, p_benchmark->sceneTarget.extent);
		VkBuffer vertex_buffers[] = {p_benchmark->vertexBuffer->getHandle()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
		vkCmdDraw(cmd, static_cast<uint32_t>(kFullscreenQuad.size()), 1, 0, 0);
		p_benchmark->m_defaultCommandBuffer->endRenderPass();
		transition_to_read(*p_benchmark->m_defaultCommandBuffer, *p_benchmark->sceneTarget.image);

		transition_to_color(*p_benchmark->m_defaultCommandBuffer, *p_benchmark->blurTargetH.image);
		p_benchmark->m_defaultCommandBuffer->beginRenderPass(*p_benchmark->blurTargetH.renderPass, *p_benchmark->blurTargetH.framebuffer);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->blurPipelineH->getHandle());
		VkDescriptorSet blur_h_set = p_benchmark->blurDescriptorH->getHandle();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->blurPipelineLayout->getHandle(), 0, 1, &blur_h_set, 0, nullptr);
		set_viewport_scissor(cmd, p_benchmark->blurTargetH.extent);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		p_benchmark->m_defaultCommandBuffer->endRenderPass();
		transition_to_read(*p_benchmark->m_defaultCommandBuffer, *p_benchmark->blurTargetH.image);

		transition_to_color(*p_benchmark->m_defaultCommandBuffer, *p_benchmark->blurTargetV.image);
		p_benchmark->m_defaultCommandBuffer->beginRenderPass(*p_benchmark->blurTargetV.renderPass, *p_benchmark->blurTargetV.framebuffer);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->blurPipelineV->getHandle());
		VkDescriptorSet blur_v_set = p_benchmark->blurDescriptorV->getHandle();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->blurPipelineLayout->getHandle(), 0, 1, &blur_v_set, 0, nullptr);
		set_viewport_scissor(cmd, p_benchmark->blurTargetV.extent);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		p_benchmark->m_defaultCommandBuffer->endRenderPass();
		transition_to_read(*p_benchmark->m_defaultCommandBuffer, *p_benchmark->blurTargetV.image);

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
	p_benchmark = std::make_unique<BloomContext>();

	vulkan_req_t req{};
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_demo_bloom_minimal", req);
	p_benchmark->initBasic(vulkan, req);

	p_benchmark->sceneTarget = create_color_target(vulkan, {p_benchmark->width, p_benchmark->height});
	p_benchmark->blurTargetH = create_color_target(vulkan, {p_benchmark->width, p_benchmark->height});
	p_benchmark->blurTargetV = create_color_target(vulkan, {p_benchmark->width, p_benchmark->height});

	auto vertexBuffer = std::make_unique<Buffer>(vulkan);
	vertexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                     sizeof(Vertex) * kFullscreenQuad.size(), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	std::vector<Vertex> vertex_data(kFullscreenQuad.begin(), kFullscreenQuad.end());
	p_benchmark->updateBuffer(vertex_data, *vertexBuffer);

	auto sceneUbo = std::make_unique<Buffer>(vulkan);
	sceneUbo->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(SceneUBO),
	                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	sceneUbo->map();
	SceneUBO scene_data{};
	scene_data.projection = glm::mat4(1.0f);
	scene_data.view = glm::mat4(1.0f);
	scene_data.model = glm::mat4(1.0f);
	memcpy(sceneUbo->m_mappedAddress, &scene_data, sizeof(scene_data));
	sceneUbo->flush(true);
	sceneUbo->unmap();

	auto blurUbo = std::make_unique<Buffer>(vulkan);
	blurUbo->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(BlurUBO),
	                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	blurUbo->map();
	BlurUBO blur_data{};
	blur_data.params = glm::vec4(1.0f, 1.5f, 0.0f, 0.0f);
	memcpy(blurUbo->m_mappedAddress, &blur_data, sizeof(blur_data));
	blurUbo->flush(true);
	blurUbo->unmap();

	auto sampler = std::make_unique<Sampler>(vulkan.device);
	sampler->create(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	                req.samplerAnisotropy, vulkan.device_properties.limits.maxSamplerAnisotropy);

	const uint32_t dummy_extent = 4;
	std::vector<uint8_t> pixel(dummy_extent * dummy_extent * 4, 255);
	auto dummyImage = std::make_shared<Image>(vulkan.device);
	dummyImage->create({dummy_extent, dummy_extent, 1}, VK_FORMAT_R8G8B8A8_UNORM,
	                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto dummyImageView = std::make_shared<ImageView>(dummyImage);
	dummyImageView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
	p_benchmark->updateImage(reinterpret_cast<const char*>(pixel.data()), pixel.size(), *dummyImage,
	                         {dummy_extent, dummy_extent, 1});

	p_benchmark->submitStaging(true, {}, {}, false);

	auto sceneDescLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	sceneDescLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	sceneDescLayout->insertBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	sceneDescLayout->create();

	auto sceneDescPool = std::make_shared<DescriptorSetPool>(sceneDescLayout);
	sceneDescPool->create(1);

	auto sceneDescriptor = std::make_unique<DescriptorSet>(sceneDescPool);
	sceneDescriptor->create();
	sceneDescriptor->setBuffer(0, *sceneUbo);
	sceneDescriptor->setCombinedImageSampler(1, *dummyImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *sampler);
	sceneDescriptor->update();

	auto blurDescLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	blurDescLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
	blurDescLayout->insertBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	blurDescLayout->create();

	auto blurDescPool = std::make_shared<DescriptorSetPool>(blurDescLayout);
	blurDescPool->create(2);

	auto blurDescriptorH = std::make_unique<DescriptorSet>(blurDescPool);
	blurDescriptorH->create();
	blurDescriptorH->setBuffer(0, *blurUbo);
	blurDescriptorH->setCombinedImageSampler(1, *p_benchmark->sceneTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *sampler);
	blurDescriptorH->update();

	auto blurDescriptorV = std::make_unique<DescriptorSet>(blurDescPool);
	blurDescriptorV->create();
	blurDescriptorV->setBuffer(0, *blurUbo);
	blurDescriptorV->setCombinedImageSampler(1, *p_benchmark->blurTargetH.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *sampler);
	blurDescriptorV->update();

	{
		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> scene_layouts = {{0, sceneDescLayout}};
		p_benchmark->scenePipelineLayout = std::make_shared<PipelineLayout>(vulkan.device);
		p_benchmark->scenePipelineLayout->create(scene_layouts);

		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> blur_layouts = {{0, blurDescLayout}};
		p_benchmark->blurPipelineLayout = std::make_shared<PipelineLayout>(vulkan.device);
		p_benchmark->blurPipelineLayout->create(blur_layouts);
	}

	{
		auto colorVert = std::make_shared<Shader>(vulkan.device);
		colorVert->create(vulkan_bloom_minimal_colorpass_vert_spv, vulkan_bloom_minimal_colorpass_vert_spv_len);
		auto colorFrag = std::make_shared<Shader>(vulkan.device);
		colorFrag->create(vulkan_bloom_minimal_colorpass_frag_spv, vulkan_bloom_minimal_colorpass_frag_spv_len);

		auto blurVert = std::make_shared<Shader>(vulkan.device);
		blurVert->create(vulkan_bloom_minimal_gaussblur_vert_spv, vulkan_bloom_minimal_gaussblur_vert_spv_len);
		auto blurFrag = std::make_shared<Shader>(vulkan.device);
		blurFrag->create(vulkan_bloom_minimal_gaussblur_frag_spv, vulkan_bloom_minimal_gaussblur_frag_spv_len);

		GraphicPipelineState sceneState;
		sceneState.setVertexBinding(0, *vertexBuffer, sizeof(Vertex));
		sceneState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, pos));
		sceneState.setVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv));
		sceneState.setVertexAttribute(2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));
		sceneState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		sceneState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		sceneState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		sceneState.m_rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		sceneState.m_rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
		sceneState.m_rasterizationState.lineWidth = 1.0f;
		sceneState.m_depthStencilState.depthTestEnable = VK_FALSE;
		sceneState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		sceneState.m_depthStencilState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		VkPipelineColorBlendAttachmentState blend{};
		blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blend.blendEnable = VK_FALSE;
		sceneState.setColorBlendAttachment(0, blend);

		GraphicPipelineState blurState;
		blurState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		blurState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		blurState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		blurState.m_rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		blurState.m_rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
		blurState.m_rasterizationState.lineWidth = 1.0f;
		blurState.m_depthStencilState.depthTestEnable = VK_FALSE;
		blurState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		blurState.m_depthStencilState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		blurState.setColorBlendAttachment(0, blend);

		ShaderPipelineState colorVertStage(VK_SHADER_STAGE_VERTEX_BIT, colorVert);
		ShaderPipelineState colorFragStage(VK_SHADER_STAGE_FRAGMENT_BIT, colorFrag);

		ShaderPipelineState blurVertStage(VK_SHADER_STAGE_VERTEX_BIT, blurVert);
		ShaderPipelineState blurFragStageH(VK_SHADER_STAGE_FRAGMENT_BIT, blurFrag);
		ShaderPipelineState blurFragStageV(VK_SHADER_STAGE_FRAGMENT_BIT, blurFrag);
		std::vector<VkSpecializationMapEntry> blur_entries(1);
		blur_entries[0].constantID = 0;
		blur_entries[0].offset = 0;
		blur_entries[0].size = sizeof(int32_t);
		int32_t blur_dir = 1;
		blurFragStageH.setSpecialization(blur_entries, sizeof(blur_dir), &blur_dir);
		blur_dir = 0;
		blurFragStageV.setSpecialization(blur_entries, sizeof(blur_dir), &blur_dir);

		p_benchmark->scenePipeline = std::make_unique<GraphicPipeline>(p_benchmark->scenePipelineLayout);
		p_benchmark->scenePipeline->create({colorVertStage, colorFragStage}, sceneState, *p_benchmark->sceneTarget.renderPass);

		p_benchmark->blurPipelineH = std::make_unique<GraphicPipeline>(p_benchmark->blurPipelineLayout);
		p_benchmark->blurPipelineH->create({blurVertStage, blurFragStageH}, blurState, *p_benchmark->blurTargetH.renderPass);

		p_benchmark->blurPipelineV = std::make_unique<GraphicPipeline>(p_benchmark->blurPipelineLayout);
		p_benchmark->blurPipelineV->create({blurVertStage, blurFragStageV}, blurState, *p_benchmark->blurTargetV.renderPass);
	}

	p_benchmark->vertexBuffer = std::move(vertexBuffer);
	p_benchmark->sceneUbo = std::move(sceneUbo);
	p_benchmark->blurUbo = std::move(blurUbo);
	p_benchmark->sampler = std::move(sampler);
	p_benchmark->dummyImage = std::move(dummyImage);
	p_benchmark->dummyImageView = std::move(dummyImageView);
	p_benchmark->sceneDescriptor = std::move(sceneDescriptor);
	p_benchmark->blurDescriptorH = std::move(blurDescriptorH);
	p_benchmark->blurDescriptorV = std::move(blurDescriptorV);
	sceneDescPool = nullptr;
	blurDescPool = nullptr;
	sceneDescLayout = nullptr;
	blurDescLayout = nullptr;

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
