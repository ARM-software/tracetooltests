// Unit test to try out vulkan graphic with variations

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_graphics_1.vert -o vulkan_graphics_1_vert.spirv
//   xxd -i vulkan_graphics_1_vert.spirv > vulkan_graphics_1_vert.inc
//   glslangValidator -V vulkan_graphics_1.frag -o vulkan_graphics_1_frag.spirv
//   xxd -i vulkan_graphics_1_frag.spirv > vulkan_graphics_1_frag.inc

#include "vulkan_graphics_1_vert.inc"
#include "vulkan_graphics_1_frag.inc"

// contains image data
//   xxd -i girl.jpg > girl.inc
#include "asset/image/girl.inc"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include <external/stb_image.h>

static void show_usage()
{
	usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return parseCmdopt(i, argc, argv, reqs);
}

using namespace tracetooltests;

// ------------------------------ benchmark definition ------------------------
typedef struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;
} Vertex;

const static std::vector<Vertex> vertices = {
	{{-0.5f, -0.5f, 0.1f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
	{{0.5f, -0.5f, 0.1f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
	{{0.5f, 0.5f, 0.1f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
	{{-0.5f, 0.5f, 0.1f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

	{{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
	{{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
	{{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
	{{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
};

const static std::vector<uint16_t> indices = {
	0, 1, 2, 2, 3, 0,
	4, 5, 6, 6, 7, 4
};

typedef struct Transform {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
} Transform;

class benchmarkContext : public GraphicContext
{
public:
	benchmarkContext() : GraphicContext() {}
	~benchmarkContext() {
		destroy();
	}
	void destroy()
	{
		DLOG3("MEM detection: graphic_1 benchmark destroy().");
		m_vertexBuffer = nullptr;
		m_indexBuffer = nullptr;
		m_transformUniformBuffer = nullptr;
		m_bgSampler = nullptr;
		m_bgImageView = nullptr;
		m_descriptor = nullptr;
		m_pipeline = nullptr;

		if (m_frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, m_frameFence, nullptr);
			m_frameFence = VK_NULL_HANDLE;
		}
	}

	// contexts and resources related with benchmark
	std::unique_ptr<Buffer> m_vertexBuffer;
	std::unique_ptr<Buffer> m_indexBuffer;
	std::unique_ptr<Buffer> m_transformUniformBuffer;

	std::unique_ptr<Sampler> m_bgSampler;
	std::unique_ptr<ImageView> m_bgImageView;

	std::unique_ptr<GraphicPipeline> m_pipeline;
	std::unique_ptr<DescriptorSet> m_descriptor;

	VkFence m_frameFence = VK_NULL_HANDLE;

};

static std::unique_ptr<benchmarkContext> p_benchmark = nullptr;
static void render(const vulkan_setup_t& vulkan);

int main(int argc, char** argv)
{
	p_benchmark = std::make_unique<benchmarkContext>();

	vulkan_req_t req;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_graphics_1", req);

	p_benchmark->initBasic(vulkan, req);

	// ------------------------ vulkan resources created -----------------------------

	/*************************** shader module **************************************/
	auto vertShader = std::make_unique<Shader>(vulkan.device);
	vertShader->create(vulkan_graphics_1_vert_spirv, vulkan_graphics_1_vert_spirv_len);

	auto fragShader = std::make_unique<Shader>(vulkan.device);
	fragShader->create(vulkan_graphics_1_frag_spirv, vulkan_graphics_1_frag_spirv_len);

	/******************* vertex/index buffers & uniform buffers *********************/
	VkDeviceSize size;
	// vbo
	size = sizeof(Vertex)*vertices.size();
	auto vertexBuffer = std::make_unique<Buffer>(vulkan);
	vertexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	p_benchmark->updateBuffer(vertices, *vertexBuffer);

	// index buffer
	size = sizeof(uint16_t)*indices.size();
	auto indexBuffer = std::make_unique<Buffer>(vulkan);
	indexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	p_benchmark->updateBuffer(indices, *indexBuffer);

	// ubo
	auto transformUniformBuffer = std::make_unique<Buffer>(vulkan);
	transformUniformBuffer->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, (VkDeviceSize)sizeof(Transform), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	transformUniformBuffer->map();

	/******************** sampled texture image for background **********************/
	// loading image
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load_from_memory(girl_jpg, girl_jpg_len, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	VkDeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		throw std::runtime_error("failed to load texture image!");
	}

	// create image/imageview to be sampled as the background
	// image/imageView shared_ptr is stored in RenderPass' attachmentInfo
	auto bgImage = std::make_shared<Image>(vulkan.device);
	bgImage->create( {(uint32_t)texWidth, (uint32_t)texHeight, 1}, VK_FORMAT_R8G8B8A8_SRGB,
	                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto bgImageView = std::make_unique<ImageView>(bgImage);
	bgImageView->create(VK_IMAGE_VIEW_TYPE_2D);

	// create sampler for bg image. sampler ptr is stored in benchmark
	auto bgSampler = std::make_unique<Sampler>(vulkan.device);
	bgSampler->create(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
	                  req.samplerAnisotropy, vulkan.device_properties.limits.maxSamplerAnisotropy);

	p_benchmark->updateImage((char*)pixels, imageSize, *bgImage, {(uint32_t)texWidth, (uint32_t)texHeight, 1});

	/*********************** initialize data and submit staging commandBuffer ***************************/
	p_benchmark->submitStaging(true, {}, {}, false);

	// ---------------------------- descriptor setup ---------------------------------

	/******************************* descriptor *************************************/
	// descriptorSet Layout:
	//   insert each binding according to the shader resources
	auto mainDescSetLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	mainDescSetLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	mainDescSetLayout->insertBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	mainDescSetLayout->create();
	// if there's one more set in shader, create another SetLayout.

	// descriptorPool
#define MAX_DESCRIPTOR_SET_SIZE 4
	auto mainDescSetPool = std::make_shared<DescriptorSetPool>(mainDescSetLayout);
	mainDescSetPool->create(MAX_DESCRIPTOR_SET_SIZE);

	// descritorSet
	auto descriptor = std::make_unique<DescriptorSet>(std::move(mainDescSetPool));
	descriptor->create();
	//configure descriptor set, and then update
	descriptor->setBuffer(0, *transformUniformBuffer);  //layout(set=0,binding=0) uniform transformBuffer { }
	descriptor->setCombinedImageSampler(1, *bgImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *bgSampler);  //layout(set=0, binding=1) uniform sampler2D
	descriptor->update();


	// ------------------------- graphic pipeline setup ------------------------------

	/***************************** pipeline layout **********************************/
	std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>>  //set num --> descriptorSetLayout
	layoutMap = { {0, mainDescSetLayout} };

	auto pipelineLayout = std::make_shared<PipelineLayout>(vulkan.device);
	pipelineLayout->create(layoutMap);
	layoutMap[0] = nullptr;  // set local variable of shared_ptr to null


	/*************************** graphicPipeline state*******************************/
	// input vertext
	GraphicPipelineState pipelineState;
	pipelineState.setVertexBinding(0, *vertexBuffer, sizeof(Vertex)); // vertexBuffer
	pipelineState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos));  //pos, color and texCoord Attrib in vertexBuffer
	pipelineState.setVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));
	pipelineState.setVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord));

	pipelineState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
	pipelineState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);


	/******************************** render pass **********************************/
	// images used as attachments
	auto colorImage = std::make_shared<Image>(vulkan.device);
	colorImage->create({p_benchmark->width, p_benchmark->height, 1}, VK_FORMAT_R8G8B8A8_UNORM,
	                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto colorImageView = std::make_shared<ImageView>(std::move(colorImage));
	colorImageView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	auto depthImage = std::make_shared<Image>(vulkan.device);
	depthImage->create({p_benchmark->width, p_benchmark->height, 1}, VK_FORMAT_D32_SFLOAT,
	                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto depthImageView = std::make_shared<ImageView>(std::move(depthImage));
	depthImageView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT);

	// attachments: set VkAttachmentDescription
	AttachmentInfo color{ 0, std::move(colorImageView), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	AttachmentInfo depth{ 1, std::move(depthImageView), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	// subpass: set VkAttachmentReference
	SubpassInfo subpass{};
	subpass.addColorAttachment(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	subpass.setDepthStencilAttachment(depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	// create renderpass
	auto renderpass = std::make_unique<RenderPass>(vulkan.device);
	renderpass->create({color,depth}, {subpass});

	VkPipelineColorBlendAttachmentState colorBlendAttachment;
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	//continue: graphicPipeline state
	pipelineState.setColorBlendAttachment(color.m_location, colorBlendAttachment);


	/********************************** framebuffer *********************************/
	auto framebuffer = std::make_shared<FrameBuffer>(vulkan.device);
	framebuffer->create(*renderpass, {p_benchmark->width, p_benchmark->height});

	/*************************** pipeline shader stage ******************************/
	ShaderPipelineState vertShaderState(VK_SHADER_STAGE_VERTEX_BIT, std::move(vertShader));
	ShaderPipelineState fragShaderState(VK_SHADER_STAGE_FRAGMENT_BIT, std::move(fragShader));

	/*************************** graphic pipeline creation **************************/
	auto pipeline = std::make_unique<GraphicPipeline>(std::move(pipelineLayout));
	pipeline->create({vertShaderState, fragShaderState}, pipelineState, *renderpass);

	/****************************** save all resources ******************************/
	p_benchmark->m_vertexBuffer = std::move(vertexBuffer);
	p_benchmark->m_indexBuffer = std::move(indexBuffer);
	p_benchmark->m_transformUniformBuffer = std::move(transformUniformBuffer);

	p_benchmark->m_bgSampler = std::move(bgSampler);
	p_benchmark->m_bgImageView = std::move(bgImageView);
	p_benchmark->m_descriptor = std::move(descriptor);

	p_benchmark->m_pipeline = std::move(pipeline);

	p_benchmark->m_renderPass = std::move(renderpass);
	p_benchmark->m_framebuffer = std::move(framebuffer);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	vkCreateFence(vulkan.device, &fenceInfo, nullptr, &p_benchmark->m_frameFence);

	/********************************** rendering ***********************************/
	render(vulkan);

	vkDeviceWaitIdle(vulkan.device);

	/********************************** exiting ***********************************/
	// set local shared_ptr to null
	bgImage = nullptr;
	mainDescSetLayout = nullptr;

	// explicitly calling destroy() on local variable
	color.destroy();
	depth.destroy();
	vertShaderState.destroy();
	fragShaderState.destroy();

	// set global unique_ptr to null to trigger deconstructor
	p_benchmark = nullptr;

	return 0;
}

void updateTransformData(Buffer& dstBuffer)
{
	static auto startTime = std::chrono::high_resolution_clock::now();

	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	Transform ubo{};
	ubo.model = glm::rotate(glm::mat4(1.0f), time*glm::radians(60.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.view = glm::lookAt(glm::vec3(2.0f, 1.0f, 1.0f), glm::vec3(0.2f, -0.1f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.proj = glm::perspective(glm::radians(45.0f), p_benchmark->width / (float) p_benchmark->height, 0.1f, 10.0f);
	ubo.proj[1][1] *= -1;

	assert(dstBuffer.m_mappedAddress!=nullptr);
	memcpy(dstBuffer.m_mappedAddress, &ubo, sizeof(ubo));
	dstBuffer.flush(true);
}

static void render(const vulkan_setup_t& vulkan)
{
	bool first_loop = true;
	benchmarking bench = vulkan.bench;

	while (p__loops--)
	{
		VkCommandBuffer defaultCmd = p_benchmark->m_defaultCommandBuffer->getHandle();

		vkWaitForFences(vulkan.device, 1, &p_benchmark->m_frameFence, VK_TRUE, UINT64_MAX);

		if (!first_loop)
		{
			bench_stop_iteration(bench);
		}
		updateTransformData(*p_benchmark->m_transformUniformBuffer);

		vkResetFences(vulkan.device, 1, &p_benchmark->m_frameFence);
		vkResetCommandBuffer(defaultCmd, 0);

		p_benchmark->m_defaultCommandBuffer->begin();
		p_benchmark->m_defaultCommandBuffer->beginRenderPass(*p_benchmark->m_renderPass, *p_benchmark->m_framebuffer);

		vkCmdBindPipeline(defaultCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->m_pipeline->getHandle());
		//one alternative: p_benchmark->m_defaultCommandBuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *p_benchmark->m_pipeline);

		// bind vertex buffer to bindings
		VkBuffer vertexBuffers[] = {p_benchmark->m_vertexBuffer->getHandle()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(defaultCmd, 0, 1, vertexBuffers, offsets);
		// bind index buffer
		vkCmdBindIndexBuffer(defaultCmd, p_benchmark->m_indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT16);

		VkDescriptorSet descriptor = p_benchmark->m_descriptor->getHandle();
		vkCmdBindDescriptorSets(defaultCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->m_pipeline->m_pipelineLayout->getHandle(), 0, 1, &descriptor, 0, nullptr);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(p_benchmark->width);
		viewport.height = static_cast<float>(p_benchmark->height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(defaultCmd, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = {0, 0};
		scissor.extent = {p_benchmark->width, p_benchmark->height};
		vkCmdSetScissor(defaultCmd, 0, 1, &scissor);
		vkCmdDrawIndexed(defaultCmd, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

		p_benchmark->m_defaultCommandBuffer->endRenderPass();
		p_benchmark->m_defaultCommandBuffer->end();

		bench_start_iteration(bench);

		// submit
		p_benchmark->submit(p_benchmark->m_defaultQueue, std::vector<std::shared_ptr<CommandBuffer>> {p_benchmark->m_defaultCommandBuffer}, p_benchmark->m_frameFence);

		first_loop = false;
	}

	vkWaitForFences(vulkan.device, 1, &p_benchmark->m_frameFence, VK_TRUE, UINT64_MAX);
	bench_stop_iteration(bench);
}
