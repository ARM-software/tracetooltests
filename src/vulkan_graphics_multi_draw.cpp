// Graphics test for VK_EXT_multi_draw using both multi-draw commands.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our shaders, generated with:
//   glslangValidator -V vulkan_graphics_1.vert -o vulkan_graphics_1_vert.spirv
//   xxd -i vulkan_graphics_1_vert.spirv > vulkan_graphics_1_vert.inc
//   glslangValidator -V vulkan_graphics_1.frag -o vulkan_graphics_1_frag.spirv
//   xxd -i vulkan_graphics_1_frag.spirv > vulkan_graphics_1_frag.inc

#include "vulkan_graphics_1_vert.inc"
#include "vulkan_graphics_1_frag.inc"

// contains image data
//   xxd -i girl.jpg > girl.inc
#include "asset/image/girl.inc"

#include <array>
#include <chrono>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <external/stb_image.h>

using namespace tracetooltests;

enum class multi_draw_mode_t
{
	alternate,
	draw,
	indexed,
};

enum class multi_draw_stride_mode_t
{
	packed,
	aligned16,
	aligned32,
};

static multi_draw_mode_t multi_draw_mode = multi_draw_mode_t::alternate;
static multi_draw_stride_mode_t multi_draw_stride_mode = multi_draw_stride_mode_t::packed;
static PFN_vkCmdDrawMultiEXT pf_vkCmdDrawMultiEXT = nullptr;
static PFN_vkCmdDrawMultiIndexedEXT pf_vkCmdDrawMultiIndexedEXT = nullptr;

static multi_draw_mode_t parse_multi_draw_mode(const char* value)
{
	if (strcmp(value, "alternate") == 0) return multi_draw_mode_t::alternate;
	if (strcmp(value, "draw") == 0) return multi_draw_mode_t::draw;
	if (strcmp(value, "indexed") == 0) return multi_draw_mode_t::indexed;
	fprintf(stderr, "Unsupported multi draw mode: %s\n", value);
	exit(EXIT_FAILURE);
}

static const char* multi_draw_mode_name(multi_draw_mode_t mode)
{
	switch (mode)
	{
		case multi_draw_mode_t::alternate: return "alternate";
		case multi_draw_mode_t::draw: return "draw";
		case multi_draw_mode_t::indexed: return "indexed";
	}
	assert(false);
	return "invalid";
}

static multi_draw_stride_mode_t parse_multi_draw_stride_mode(const char* value)
{
	if (strcmp(value, "packed") == 0) return multi_draw_stride_mode_t::packed;
	if (strcmp(value, "aligned16") == 0) return multi_draw_stride_mode_t::aligned16;
	if (strcmp(value, "aligned32") == 0) return multi_draw_stride_mode_t::aligned32;
	fprintf(stderr, "Unsupported multi draw stride mode: %s\n", value);
	exit(EXIT_FAILURE);
}

static const char* multi_draw_stride_mode_name(multi_draw_stride_mode_t mode)
{
	switch (mode)
	{
		case multi_draw_stride_mode_t::packed: return "packed";
		case multi_draw_stride_mode_t::aligned16: return "aligned16";
		case multi_draw_stride_mode_t::aligned32: return "aligned32";
	}
	assert(false);
	return "invalid";
}

static void show_usage()
{
	printf("-i/--image-output      Save an image of the output to disk\n");
	printf("-m/--multi-draw-mode M Use one of: alternate, draw, indexed\n");
	printf("-s/--stride-mode S     Use one of: packed, aligned16, aligned32\n");
	usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-i", "--image-output"))
	{
		reqs.options["image_output"] = true;
		return true;
	}
	if (match(argv[i], "-m", "--multi-draw-mode"))
	{
		multi_draw_mode = parse_multi_draw_mode(get_string_arg(argv, ++i, argc));
		return true;
	}
	if (match(argv[i], "-s", "--stride-mode"))
	{
		multi_draw_stride_mode = parse_multi_draw_stride_mode(get_string_arg(argv, ++i, argc));
		return true;
	}
	return parseCmdopt(i, argc, argv, reqs);
}

typedef struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;
} Vertex;

const static std::vector<Vertex> vertices = {
	// Top row: non-indexed multi-draw
	{{-0.90f,  0.70f,  0.10f}, {1.00f, 0.20f, 0.20f}, {0.0f, 0.0f}},
	{{-0.50f,  0.70f,  0.10f}, {1.00f, 0.20f, 0.20f}, {1.0f, 0.0f}},
	{{-0.50f,  0.25f,  0.10f}, {1.00f, 0.20f, 0.20f}, {1.0f, 1.0f}},
	{{-0.90f,  0.70f,  0.10f}, {1.00f, 0.20f, 0.20f}, {0.0f, 0.0f}},
	{{-0.50f,  0.25f,  0.10f}, {1.00f, 0.20f, 0.20f}, {1.0f, 1.0f}},
	{{-0.90f,  0.25f,  0.10f}, {1.00f, 0.20f, 0.20f}, {0.0f, 1.0f}},

	{{ 0.10f,  0.70f, -0.20f}, {0.20f, 1.00f, 0.20f}, {0.0f, 0.0f}},
	{{ 0.50f,  0.70f, -0.20f}, {0.20f, 1.00f, 0.20f}, {1.0f, 0.0f}},
	{{ 0.50f,  0.25f, -0.20f}, {0.20f, 1.00f, 0.20f}, {1.0f, 1.0f}},
	{{ 0.10f,  0.70f, -0.20f}, {0.20f, 1.00f, 0.20f}, {0.0f, 0.0f}},
	{{ 0.50f,  0.25f, -0.20f}, {0.20f, 1.00f, 0.20f}, {1.0f, 1.0f}},
	{{ 0.10f,  0.25f, -0.20f}, {0.20f, 1.00f, 0.20f}, {0.0f, 1.0f}},

	// Bottom row: indexed multi-draw
	{{-0.45f, -0.20f,  0.00f}, {0.20f, 0.20f, 1.00f}, {0.0f, 0.0f}},
	{{-0.05f, -0.20f,  0.00f}, {0.20f, 0.20f, 1.00f}, {1.0f, 0.0f}},
	{{-0.05f, -0.70f,  0.00f}, {0.20f, 0.20f, 1.00f}, {1.0f, 1.0f}},
	{{-0.45f, -0.20f,  0.00f}, {0.20f, 0.20f, 1.00f}, {0.0f, 0.0f}},
	{{-0.05f, -0.70f,  0.00f}, {0.20f, 0.20f, 1.00f}, {1.0f, 1.0f}},
	{{-0.45f, -0.70f,  0.00f}, {0.20f, 0.20f, 1.00f}, {0.0f, 1.0f}},

	{{ 0.55f, -0.20f, -0.35f}, {1.00f, 0.90f, 0.20f}, {0.0f, 0.0f}},
	{{ 0.95f, -0.20f, -0.35f}, {1.00f, 0.90f, 0.20f}, {1.0f, 0.0f}},
	{{ 0.95f, -0.70f, -0.35f}, {1.00f, 0.90f, 0.20f}, {1.0f, 1.0f}},
	{{ 0.55f, -0.20f, -0.35f}, {1.00f, 0.90f, 0.20f}, {0.0f, 0.0f}},
	{{ 0.95f, -0.70f, -0.35f}, {1.00f, 0.90f, 0.20f}, {1.0f, 1.0f}},
	{{ 0.55f, -0.70f, -0.35f}, {1.00f, 0.90f, 0.20f}, {0.0f, 1.0f}},
};

const static std::vector<uint16_t> indices = {
	12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23
};

const static std::array<VkMultiDrawInfoEXT, 2> vertex_draws = {{
	{0, 6},
	{6, 6}
}};

const static std::array<VkMultiDrawIndexedInfoEXT, 2> indexed_draws = {{
	{0, 6, 0},
	{6, 6, 0}
}};

template <typename T, size_t Stride>
struct StridedDrawInfo
{
	static_assert((Stride % 4) == 0);
	static_assert(Stride >= sizeof(T));

	T info;
	std::array<uint8_t, Stride - sizeof(T)> padding{};
};

using strided_vertex_draw_info_16_t = StridedDrawInfo<VkMultiDrawInfoEXT, 16>;
using strided_vertex_draw_info_32_t = StridedDrawInfo<VkMultiDrawInfoEXT, 32>;
using strided_indexed_draw_info_16_t = StridedDrawInfo<VkMultiDrawIndexedInfoEXT, 16>;
using strided_indexed_draw_info_32_t = StridedDrawInfo<VkMultiDrawIndexedInfoEXT, 32>;

static_assert(sizeof(strided_vertex_draw_info_16_t) == 16);
static_assert(sizeof(strided_vertex_draw_info_32_t) == 32);
static_assert(sizeof(strided_indexed_draw_info_16_t) == 16);
static_assert(sizeof(strided_indexed_draw_info_32_t) == 32);

const static std::array<strided_vertex_draw_info_16_t, 2> vertex_draws_aligned16 = {{
	{{0, 6}, {}},
	{{6, 6}, {}}
}};

const static std::array<strided_vertex_draw_info_32_t, 2> vertex_draws_aligned32 = {{
	{{0, 6}, {}},
	{{6, 6}, {}}
}};

const static std::array<strided_indexed_draw_info_16_t, 2> indexed_draws_aligned16 = {{
	{{0, 6, 0}, {}},
	{{6, 6, 0}, {}}
}};

const static std::array<strided_indexed_draw_info_32_t, 2> indexed_draws_aligned32 = {{
	{{0, 6, 0}, {}},
	{{6, 6, 0}, {}}
}};

template <typename T>
struct multi_draw_batch_t
{
	uint32_t drawCount = 0;
	const T* data = nullptr;
	uint32_t stride = 0;
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
		DLOG3("MEM detection: graphics_multi_draw benchmark destroy().");
		m_vertexBuffer = nullptr;
		m_indexBuffer = nullptr;
		m_transformUniformBuffer = nullptr;
		m_bgSampler = nullptr;
		m_bgImageView = nullptr;
		m_descriptor = nullptr;
		m_pipeline = nullptr;
		m_pipelineLayout = nullptr;

		if (m_frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, m_frameFence, nullptr);
			m_frameFence = VK_NULL_HANDLE;
		}
	}

	std::unique_ptr<Buffer> m_vertexBuffer;
	std::unique_ptr<Buffer> m_indexBuffer;
	std::unique_ptr<Buffer> m_transformUniformBuffer;

	std::unique_ptr<Sampler> m_bgSampler;
	std::unique_ptr<ImageView> m_bgImageView;

	std::unique_ptr<GraphicPipeline> m_pipeline;
	std::shared_ptr<PipelineLayout> m_pipelineLayout;
	std::unique_ptr<DescriptorSet> m_descriptor;

	VkFence m_frameFence = VK_NULL_HANDLE;
};

static std::unique_ptr<benchmarkContext> p_benchmark = nullptr;
static void render(const vulkan_setup_t& vulkan);
static void save_texture_debug(const vulkan_setup_t& vulkan, Image& image, uint32_t width, uint32_t height);
static void record_multi_draws(const vulkan_setup_t& vulkan, VkCommandBuffer command_buffer, bool indexed_first);
template <typename T> static uint32_t resolve_multi_draw_stride();
static multi_draw_batch_t<VkMultiDrawInfoEXT> get_vertex_draw_batch();
static multi_draw_batch_t<VkMultiDrawIndexedInfoEXT> get_indexed_draw_batch();

int main(int argc, char** argv)
{
	p_benchmark = std::make_unique<benchmarkContext>();

	vulkan_req_t req;
	VkPhysicalDeviceMultiDrawFeaturesEXT multi_draw_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT, nullptr };
	multi_draw_features.multiDraw = VK_TRUE;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.apiVersion = VK_API_VERSION_1_1;
	req.minApiVersion = VK_API_VERSION_1_1;
	req.device_extensions.push_back("VK_EXT_multi_draw");
	req.extension_features = reinterpret_cast<VkBaseInStructure*>(&multi_draw_features);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_graphics_multi_draw", req);

	pf_vkCmdDrawMultiEXT = reinterpret_cast<PFN_vkCmdDrawMultiEXT>(vkGetDeviceProcAddr(vulkan.device, "vkCmdDrawMultiEXT"));
	pf_vkCmdDrawMultiIndexedEXT = reinterpret_cast<PFN_vkCmdDrawMultiIndexedEXT>(vkGetDeviceProcAddr(vulkan.device, "vkCmdDrawMultiIndexedEXT"));
	assert(pf_vkCmdDrawMultiEXT);
	assert(pf_vkCmdDrawMultiIndexedEXT);

	VkPhysicalDeviceMultiDrawFeaturesEXT queried_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT, nullptr };
	VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &queried_features };
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &features2);

	VkPhysicalDeviceMultiDrawPropertiesEXT multi_draw_properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &multi_draw_properties };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &props2);

	printf("VK_EXT_multi_draw mode: %s\n", multi_draw_mode_name(multi_draw_mode));
	printf("VK_EXT_multi_draw stride mode: %s\n", multi_draw_stride_mode_name(multi_draw_stride_mode));
	printf("VK_EXT_multi_draw feature multiDraw = %s\n", queried_features.multiDraw ? "true" : "false");
	printf("VK_EXT_multi_draw maxMultiDrawCount = %u\n", multi_draw_properties.maxMultiDrawCount);
	printf("VK_EXT_multi_draw vertex stride = %u\n", resolve_multi_draw_stride<VkMultiDrawInfoEXT>());
	printf("VK_EXT_multi_draw indexed stride = %u\n", resolve_multi_draw_stride<VkMultiDrawIndexedInfoEXT>());

	if (!queried_features.multiDraw || multi_draw_properties.maxMultiDrawCount < vertex_draws.size())
	{
		printf("VK_EXT_multi_draw does not support a batched drawCount of %zu on this device\n", vertex_draws.size());
		test_done(vulkan);
		p_benchmark = nullptr;
		return 77;
	}

	p_benchmark->initBasic(vulkan, req);

	auto vertShader = std::make_unique<Shader>(vulkan.device);
	vertShader->create(vulkan_graphics_1_vert_spirv, vulkan_graphics_1_vert_spirv_len);

	auto fragShader = std::make_unique<Shader>(vulkan.device);
	fragShader->create(vulkan_graphics_1_frag_spirv, vulkan_graphics_1_frag_spirv_len);

	VkDeviceSize size = sizeof(Vertex) * vertices.size();
	auto vertexBuffer = std::make_unique<Buffer>(vulkan);
	vertexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	p_benchmark->updateBuffer(vertices, *vertexBuffer);

	size = sizeof(uint16_t) * indices.size();
	auto indexBuffer = std::make_unique<Buffer>(vulkan);
	indexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	p_benchmark->updateBuffer(indices, *indexBuffer);

	auto transformUniformBuffer = std::make_unique<Buffer>(vulkan);
	transformUniformBuffer->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, static_cast<VkDeviceSize>(sizeof(Transform)),
	                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	transformUniformBuffer->map();

	int texWidth = 0;
	int texHeight = 0;
	int texChannels = 0;
	stbi_uc* pixels = stbi_load_from_memory(girl_jpg, girl_jpg_len, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	VkDeviceSize imageSize = texWidth * texHeight * 4;
	if (!pixels)
	{
		throw std::runtime_error("failed to load texture image!");
	}

	auto bgImage = std::make_shared<Image>(vulkan.device);
	VkImageUsageFlags bgUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	if (req.options.count("image_output"))
	{
		bgUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	bgImage->create({static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1}, VK_FORMAT_R8G8B8A8_SRGB,
	                bgUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto bgImageView = std::make_unique<ImageView>(bgImage);
	bgImageView->create(VK_IMAGE_VIEW_TYPE_2D);

	auto bgSampler = std::make_unique<Sampler>(vulkan.device);
	bgSampler->create(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
	                  req.samplerAnisotropy, vulkan.device_properties.limits.maxSamplerAnisotropy);

	p_benchmark->updateImage(reinterpret_cast<char*>(pixels), imageSize, *bgImage, {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1});
	stbi_image_free(pixels);

	p_benchmark->submitStaging(true, {}, {}, false);
	if (req.options.count("image_output"))
	{
		save_texture_debug(vulkan, *bgImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
	}

	auto mainDescSetLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	mainDescSetLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	mainDescSetLayout->insertBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	mainDescSetLayout->create();

#define MAX_DESCRIPTOR_SET_SIZE 4
	auto mainDescSetPool = std::make_shared<DescriptorSetPool>(vulkan.device);
	mainDescSetPool->create(MAX_DESCRIPTOR_SET_SIZE,
	                        {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_DESCRIPTOR_SET_SIZE},
	                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_DESCRIPTOR_SET_SIZE}});

	auto descriptor = std::make_unique<DescriptorSet>(std::move(mainDescSetPool));
	descriptor->create(*mainDescSetLayout);
	descriptor->setBuffer(0, 0, *transformUniformBuffer);
	descriptor->setCombinedImageSampler(1, 0, *bgImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *bgSampler);
	descriptor->update();

	std::vector<VkDescriptorSetLayout> setLayouts = { mainDescSetLayout->getHandle() };
	auto pipelineLayout = std::make_shared<PipelineLayout>(vulkan.device);
	pipelineLayout->create(setLayouts);

	GraphicPipelineState pipelineState;
	pipelineState.setVertexBinding(0, *vertexBuffer, sizeof(Vertex));
	pipelineState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos));
	pipelineState.setVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));
	pipelineState.setVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord));
	pipelineState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
	pipelineState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
	pipelineState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);

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

	AttachmentInfo color{ 0, *colorImageView, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	AttachmentInfo depth{ 1, *depthImageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	SubpassInfo subpass{};
	subpass.addColorAttachment(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	subpass.setDepthStencilAttachment(depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	auto renderpass = std::make_unique<RenderPass>(vulkan.device);
	renderpass->create({color, depth}, {subpass});

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	pipelineState.setColorBlendAttachment(color.m_location, colorBlendAttachment);

	auto framebuffer = std::make_shared<FrameBuffer>(vulkan.device);
	framebuffer->create(*renderpass, {colorImageView, depthImageView}, {p_benchmark->width, p_benchmark->height});

	ShaderPipelineState vertShaderState(VK_SHADER_STAGE_VERTEX_BIT, std::move(vertShader));
	ShaderPipelineState fragShaderState(VK_SHADER_STAGE_FRAGMENT_BIT, std::move(fragShader));

	auto pipeline = std::make_unique<GraphicPipeline>(vulkan.device);
	pipeline->create(pipelineLayout->getHandle(), {vertShaderState, fragShaderState}, pipelineState, *renderpass);

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)vertexBuffer->getHandle(), "graphics_multi_draw_vertex");
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)indexBuffer->getHandle(), "graphics_multi_draw_index");
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)transformUniformBuffer->getHandle(), "graphics_multi_draw_ubo");
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline->getHandle(), "graphics_multi_draw_pipeline");

	p_benchmark->m_vertexBuffer = std::move(vertexBuffer);
	p_benchmark->m_indexBuffer = std::move(indexBuffer);
	p_benchmark->m_transformUniformBuffer = std::move(transformUniformBuffer);
	p_benchmark->m_bgSampler = std::move(bgSampler);
	p_benchmark->m_bgImageView = std::move(bgImageView);
	p_benchmark->m_descriptor = std::move(descriptor);
	p_benchmark->m_pipelineLayout = std::move(pipelineLayout);
	p_benchmark->m_pipeline = std::move(pipeline);
	p_benchmark->m_renderPass = std::move(renderpass);
	p_benchmark->m_framebuffer = std::move(framebuffer);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VkResult result = vkCreateFence(vulkan.device, &fenceInfo, nullptr, &p_benchmark->m_frameFence);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_FENCE, (uint64_t)p_benchmark->m_frameFence, "graphics_multi_draw_fence");

	render(vulkan);

	vkDeviceWaitIdle(vulkan.device);

	bgImage = nullptr;
	mainDescSetLayout = nullptr;

	color.destroy();
	depth.destroy();
	vertShaderState.destroy();
	fragShaderState.destroy();

	p_benchmark = nullptr;
	return 0;
}

static void updateTransformData(Buffer& dstBuffer)
{
	static auto startTime = std::chrono::high_resolution_clock::now();
	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	Transform ubo{};
	ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(35.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.view = glm::lookAt(glm::vec3(2.0f, 1.0f, 1.0f), glm::vec3(0.2f, -0.1f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.proj = glm::perspective(glm::radians(45.0f), p_benchmark->width / (float)p_benchmark->height, 0.1f, 10.0f);
	ubo.proj[1][1] *= -1;

	assert(dstBuffer.m_mappedAddress != nullptr);
	memcpy(dstBuffer.m_mappedAddress, &ubo, sizeof(ubo));
	dstBuffer.flush(true);
}

static void save_texture_debug(const vulkan_setup_t& vulkan, Image& image, uint32_t width, uint32_t height)
{
	const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;
	auto staging = std::make_unique<Buffer>(vulkan);
	staging->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT, size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	auto commandBuffer = std::make_shared<CommandBuffer>(p_benchmark->m_defaultCommandPool);
	commandBuffer->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	commandBuffer->imageMemoryBarrier(image, image.m_imageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                                  VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
	                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	commandBuffer->copyImageToBuffer(image, *staging, 0, {width, height, 1});
	commandBuffer->bufferMemoryBarrier(*staging, 0, size,
	                                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
	                                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
	commandBuffer->imageMemoryBarrier(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
	                                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	commandBuffer->end();

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence = VK_NULL_HANDLE;
	VkResult result = vkCreateFence(vulkan.device, &fenceInfo, nullptr, &fence);
	check(result);
	p_benchmark->submit(p_benchmark->m_defaultQueue, std::vector<std::shared_ptr<CommandBuffer>> {commandBuffer}, fence, {}, {}, false);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
	vkDestroyFence(vulkan.device, fence, nullptr);

	test_save_image(vulkan, "texture_upload.png", staging->getMemory(), 0, width, height, image.m_format);
}

template <typename T>
static uint32_t resolve_multi_draw_stride()
{
	switch (multi_draw_stride_mode)
	{
		case multi_draw_stride_mode_t::packed: return sizeof(T);
		case multi_draw_stride_mode_t::aligned16: return 16;
		case multi_draw_stride_mode_t::aligned32: return 32;
	}

	assert(false);
	return 0;
}

static multi_draw_batch_t<VkMultiDrawInfoEXT> get_vertex_draw_batch()
{
	const uint32_t stride = resolve_multi_draw_stride<VkMultiDrawInfoEXT>();
	assert((stride % 4) == 0);
	assert(stride >= sizeof(VkMultiDrawInfoEXT));

	switch (multi_draw_stride_mode)
	{
		case multi_draw_stride_mode_t::packed:
			return { static_cast<uint32_t>(vertex_draws.size()), vertex_draws.data(), stride };
		case multi_draw_stride_mode_t::aligned16:
			return { static_cast<uint32_t>(vertex_draws_aligned16.size()), &vertex_draws_aligned16[0].info, stride };
		case multi_draw_stride_mode_t::aligned32:
			return { static_cast<uint32_t>(vertex_draws_aligned32.size()), &vertex_draws_aligned32[0].info, stride };
	}

	assert(false);
	return {};
}

static multi_draw_batch_t<VkMultiDrawIndexedInfoEXT> get_indexed_draw_batch()
{
	const uint32_t stride = resolve_multi_draw_stride<VkMultiDrawIndexedInfoEXT>();
	assert((stride % 4) == 0);
	assert(stride >= sizeof(VkMultiDrawIndexedInfoEXT));

	switch (multi_draw_stride_mode)
	{
		case multi_draw_stride_mode_t::packed:
			return { static_cast<uint32_t>(indexed_draws.size()), indexed_draws.data(), stride };
		case multi_draw_stride_mode_t::aligned16:
			return { static_cast<uint32_t>(indexed_draws_aligned16.size()), &indexed_draws_aligned16[0].info, stride };
		case multi_draw_stride_mode_t::aligned32:
			return { static_cast<uint32_t>(indexed_draws_aligned32.size()), &indexed_draws_aligned32[0].info, stride };
	}

	assert(false);
	return {};
}

static void record_multi_draws(const vulkan_setup_t& vulkan, VkCommandBuffer command_buffer, bool indexed_first)
{
	const auto vertex_batch = get_vertex_draw_batch();
	const auto indexed_batch = get_indexed_draw_batch();

	auto record_draw = [&](bool indexed) {
		if (indexed)
		{
			test_marker_mention(vulkan, "Issuing vkCmdDrawMultiIndexedEXT", VK_OBJECT_TYPE_BUFFER,
			                    (uint64_t)p_benchmark->m_indexBuffer->getHandle());
			pf_vkCmdDrawMultiIndexedEXT(command_buffer,
			                           indexed_batch.drawCount,
			                           indexed_batch.data,
			                           1,
			                           0,
			                           indexed_batch.stride,
			                           nullptr);
			return;
		}

		test_marker_mention(vulkan, "Issuing vkCmdDrawMultiEXT", VK_OBJECT_TYPE_BUFFER,
		                    (uint64_t)p_benchmark->m_vertexBuffer->getHandle());
		pf_vkCmdDrawMultiEXT(command_buffer,
		                    vertex_batch.drawCount,
		                    vertex_batch.data,
		                    1,
		                    0,
		                    vertex_batch.stride);
	};

	if (multi_draw_mode == multi_draw_mode_t::draw)
	{
		record_draw(false);
		return;
	}

	if (multi_draw_mode == multi_draw_mode_t::indexed)
	{
		record_draw(true);
		return;
	}

	record_draw(indexed_first);
	record_draw(!indexed_first);
}

static void render(const vulkan_setup_t& vulkan)
{
	bool first_loop = true;
	uint32_t frame_index = 0;
	benchmarking bench = vulkan.bench;

	while (p__loops--)
	{
		VkCommandBuffer defaultCmd = p_benchmark->m_defaultCommandBuffer->getHandle();

		VkResult result = vkWaitForFences(vulkan.device, 1, &p_benchmark->m_frameFence, VK_TRUE, UINT64_MAX);
		check(result);

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

		VkBuffer vertexBuffers[] = {p_benchmark->m_vertexBuffer->getHandle()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(defaultCmd, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(defaultCmd, p_benchmark->m_indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT16);

		VkDescriptorSet descriptor = p_benchmark->m_descriptor->getHandle();
		vkCmdBindDescriptorSets(defaultCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->m_pipelineLayout->getHandle(), 0, 1, &descriptor, 0, nullptr);

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

		record_multi_draws(vulkan, defaultCmd, (frame_index % 2) == 1);

		p_benchmark->m_defaultCommandBuffer->endRenderPass();
		p_benchmark->m_defaultCommandBuffer->end();

		bench_start_iteration(bench);
		p_benchmark->submit(p_benchmark->m_defaultQueue, std::vector<std::shared_ptr<CommandBuffer>> {p_benchmark->m_defaultCommandBuffer}, p_benchmark->m_frameFence);

		first_loop = false;
		frame_index++;
	}

	VkResult result = vkWaitForFences(vulkan.device, 1, &p_benchmark->m_frameFence, VK_TRUE, UINT64_MAX);
	check(result);
	bench_stop_iteration(bench);
	p_benchmark->saveImageOutput();
}
