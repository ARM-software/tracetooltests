// Minimal VkQuake2-inspired render loop using original shaders.
// Focuses on the main render passes: world -> warp -> UI/postprocess.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// VkQuake2 shader blobs (SPIR-V C arrays).
// Original GLSL sources are mirrored in `src/vkquake2/`.
#include "src/vkquake2/spirv/basic_vert.c"
#include "src/vkquake2/spirv/basic_frag.c"
#include "src/vkquake2/spirv/basic_color_quad_vert.c"
#include "src/vkquake2/spirv/basic_color_quad_frag.c"
#include "src/vkquake2/spirv/model_vert.c"
#include "src/vkquake2/spirv/model_frag.c"
#include "src/vkquake2/spirv/particle_vert.c"
#include "src/vkquake2/spirv/sprite_vert.c"
#include "src/vkquake2/spirv/beam_vert.c"
#include "src/vkquake2/spirv/skybox_vert.c"
#include "src/vkquake2/spirv/d_light_vert.c"
#include "src/vkquake2/spirv/polygon_lmap_vert.c"
#include "src/vkquake2/spirv/polygon_lmap_frag.c"
#include "src/vkquake2/spirv/polygon_warp_vert.c"
#include "src/vkquake2/spirv/postprocess_vert.c"
#include "src/vkquake2/spirv/postprocess_frag.c"
#include "src/vkquake2/spirv/world_warp_vert.c"
#include "src/vkquake2/spirv/world_warp_frag.c"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>

using namespace tracetooltests;

static void show_usage()
{
	printf("-i/--image-output      Save an image of the output to disk\n");
	usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-i", "--image-output"))
	{
		reqs.options["image_output"] = true;
		return true;
	}
	return parseCmdopt(i, argc, argv, reqs);
}

struct Texture
{
	std::shared_ptr<Image> image;
	std::shared_ptr<ImageView> view;
};

struct VertexPosUV
{
	glm::vec3 pos;
	glm::vec2 uv;
};

struct VertexPosColorUV
{
	glm::vec3 pos;
	glm::vec4 color;
	glm::vec2 uv;
};

struct VertexPosUVLmap
{
	glm::vec3 pos;
	glm::vec2 uv;
	glm::vec2 lmap;
};

struct VertexPos
{
	glm::vec3 pos;
};

struct VertexPosColor
{
	glm::vec3 pos;
	glm::vec3 color;
};

struct VertexUI
{
	glm::vec2 pos;
	glm::vec2 uv;
};

struct UboLmap
{
	glm::mat4 model;
	alignas(16) float viewLightmaps;
	glm::vec3 pad;
};

struct UboWarp
{
	glm::mat4 model;
	glm::vec4 color;
	float time;
	float scroll;
	glm::vec2 pad;
};

struct UboModel
{
	glm::mat4 model;
	alignas(16) int textured;
	glm::ivec3 pad;
};

struct UboSprite
{
	alignas(16) float alpha;
	glm::vec3 pad;
};

struct UboSky
{
	glm::mat4 model;
};

struct UboBeam
{
	glm::vec4 color;
};

struct UboDLight
{
	glm::mat4 mvp;
};

struct UboImageTransform
{
	glm::vec2 offset;
	glm::vec2 scale;
	glm::vec2 uvOffset;
	glm::vec2 uvScale;
};

struct UboColorQuad
{
	glm::vec2 offset;
	glm::vec2 scale;
	glm::vec4 color;
};

static Texture create_texture(GraphicContext& ctx, VkDevice device, const std::vector<uint8_t>& data,
                              uint32_t width, uint32_t height, VkFormat format)
{
	Texture tex;
	tex.image = std::make_shared<Image>(device);
	tex.image->create({width, height, 1}, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	tex.view = std::make_shared<ImageView>(tex.image);
	tex.view->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	ctx.updateImage(data, *tex.image, {width, height, 1});
	return tex;
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

static VkPipelineColorBlendAttachmentState blend_state(bool enable,
                                                       VkBlendFactor src = VK_BLEND_FACTOR_SRC_ALPHA,
                                                       VkBlendFactor dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
{
	VkPipelineColorBlendAttachmentState blend{};
	blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blend.blendEnable = enable ? VK_TRUE : VK_FALSE;
	blend.srcColorBlendFactor = src;
	blend.dstColorBlendFactor = dst;
	blend.colorBlendOp = VK_BLEND_OP_ADD;
	blend.srcAlphaBlendFactor = src;
	blend.dstAlphaBlendFactor = dst;
	blend.alphaBlendOp = VK_BLEND_OP_ADD;
	return blend;
}

class VkQuake2Context : public GraphicContext
{
public:
	VkQuake2Context() : GraphicContext() {}
	~VkQuake2Context() override { destroy(); }

	void destroy() override
	{
		baseTex = {};
		lightmapTex = {};
		skyTex = {};
		uiTex = {};
		worldColor = {};
		warpColor = {};
		uiColor = {};
		worldDepthView = nullptr;
		worldDepth = nullptr;

		worldPass = nullptr;
		warpPass = nullptr;
		uiPass = nullptr;
		worldFB = nullptr;
		warpFB = nullptr;
		uiFB = nullptr;

		sampler = nullptr;
		samplerLayout = nullptr;
		uboLayout = nullptr;
		samplerPool = nullptr;
		uboPool = nullptr;

		ds_base = nullptr;
		ds_lightmap = nullptr;
		ds_sky = nullptr;
		ds_ui = nullptr;
		ds_worldColor = nullptr;
		ds_warpColor = nullptr;
		ds_ubo_world = nullptr;
		ds_ubo_water = nullptr;
		ds_ubo_model = nullptr;
		ds_ubo_sprite = nullptr;
		ds_ubo_sky = nullptr;
		ds_ubo_basic = nullptr;
		ds_ubo_beam = nullptr;
		ds_ubo_dlight = nullptr;
		ds_ubo_colorquad = nullptr;

		vb_world = nullptr;
		vb_water = nullptr;
		vb_model = nullptr;
		vb_sprite = nullptr;
		vb_particle = nullptr;
		vb_beam = nullptr;
		vb_dlight = nullptr;
		vb_ui = nullptr;
		ib_ui = nullptr;

		ubo_world = nullptr;
		ubo_water = nullptr;
		ubo_model = nullptr;
		ubo_sprite = nullptr;
		ubo_sky = nullptr;
		ubo_basic = nullptr;
		ubo_beam = nullptr;
		ubo_dlight = nullptr;
		ubo_colorquad = nullptr;

		layout_sampler_ubo_pc = nullptr;
		layout_sampler_ubo_lmap_pc = nullptr;
		layout_sampler_pc = nullptr;
		layout_sampler_frag_pc = nullptr;
		layout_sampler_ubo = nullptr;
		layout_ubo_pc = nullptr;
		layout_ubo = nullptr;

		pipe_world_lmap = nullptr;
		pipe_water = nullptr;
		pipe_model = nullptr;
		pipe_sprite = nullptr;
		pipe_particle = nullptr;
		pipe_sky = nullptr;
		pipe_beam = nullptr;
		pipe_dlight = nullptr;
		pipe_basic = nullptr;
		pipe_colorquad = nullptr;
		pipe_worldwarp = nullptr;
		pipe_postprocess = nullptr;

		if (frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, frameFence, nullptr);
			frameFence = VK_NULL_HANDLE;
		}

		GraphicContext::destroy();
		BasicContext::destroy();
	}

	Texture baseTex;
	Texture lightmapTex;
	Texture skyTex;
	Texture uiTex;

	Texture worldColor;
	Texture warpColor;
	Texture uiColor;
	std::shared_ptr<Image> worldDepth;
	std::shared_ptr<ImageView> worldDepthView;

	std::shared_ptr<RenderPass> worldPass;
	std::shared_ptr<RenderPass> warpPass;
	std::shared_ptr<RenderPass> uiPass;
	std::shared_ptr<FrameBuffer> worldFB;
	std::shared_ptr<FrameBuffer> warpFB;
	std::shared_ptr<FrameBuffer> uiFB;

	std::unique_ptr<Sampler> sampler;
	std::shared_ptr<DescriptorSetLayout> samplerLayout;
	std::shared_ptr<DescriptorSetLayout> uboLayout;
	std::shared_ptr<DescriptorSetPool> samplerPool;
	std::shared_ptr<DescriptorSetPool> uboPool;

	std::unique_ptr<DescriptorSet> ds_base;
	std::unique_ptr<DescriptorSet> ds_lightmap;
	std::unique_ptr<DescriptorSet> ds_sky;
	std::unique_ptr<DescriptorSet> ds_ui;
	std::unique_ptr<DescriptorSet> ds_worldColor;
	std::unique_ptr<DescriptorSet> ds_warpColor;

	std::unique_ptr<DescriptorSet> ds_ubo_world;
	std::unique_ptr<DescriptorSet> ds_ubo_water;
	std::unique_ptr<DescriptorSet> ds_ubo_model;
	std::unique_ptr<DescriptorSet> ds_ubo_sprite;
	std::unique_ptr<DescriptorSet> ds_ubo_sky;
	std::unique_ptr<DescriptorSet> ds_ubo_basic;
	std::unique_ptr<DescriptorSet> ds_ubo_beam;
	std::unique_ptr<DescriptorSet> ds_ubo_dlight;
	std::unique_ptr<DescriptorSet> ds_ubo_colorquad;

	std::unique_ptr<Buffer> vb_world;
	std::unique_ptr<Buffer> vb_water;
	std::unique_ptr<Buffer> vb_model;
	std::unique_ptr<Buffer> vb_sprite;
	std::unique_ptr<Buffer> vb_particle;
	std::unique_ptr<Buffer> vb_beam;
	std::unique_ptr<Buffer> vb_dlight;
	std::unique_ptr<Buffer> vb_ui;
	std::unique_ptr<Buffer> ib_ui;

	std::unique_ptr<Buffer> ubo_world;
	std::unique_ptr<Buffer> ubo_water;
	std::unique_ptr<Buffer> ubo_model;
	std::unique_ptr<Buffer> ubo_sprite;
	std::unique_ptr<Buffer> ubo_sky;
	std::unique_ptr<Buffer> ubo_basic;
	std::unique_ptr<Buffer> ubo_beam;
	std::unique_ptr<Buffer> ubo_dlight;
	std::unique_ptr<Buffer> ubo_colorquad;

	std::shared_ptr<PipelineLayout> layout_sampler_ubo_pc;
	std::shared_ptr<PipelineLayout> layout_sampler_ubo_lmap_pc;
	std::shared_ptr<PipelineLayout> layout_sampler_pc;
	std::shared_ptr<PipelineLayout> layout_sampler_frag_pc;
	std::shared_ptr<PipelineLayout> layout_sampler_ubo;
	std::shared_ptr<PipelineLayout> layout_ubo_pc;
	std::shared_ptr<PipelineLayout> layout_ubo;

	std::unique_ptr<GraphicPipeline> pipe_world_lmap;
	std::unique_ptr<GraphicPipeline> pipe_water;
	std::unique_ptr<GraphicPipeline> pipe_model;
	std::unique_ptr<GraphicPipeline> pipe_sprite;
	std::unique_ptr<GraphicPipeline> pipe_particle;
	std::unique_ptr<GraphicPipeline> pipe_sky;
	std::unique_ptr<GraphicPipeline> pipe_beam;
	std::unique_ptr<GraphicPipeline> pipe_dlight;
	std::unique_ptr<GraphicPipeline> pipe_basic;
	std::unique_ptr<GraphicPipeline> pipe_colorquad;
	std::unique_ptr<GraphicPipeline> pipe_worldwarp;
	std::unique_ptr<GraphicPipeline> pipe_postprocess;

	VkFence frameFence = VK_NULL_HANDLE;

	glm::mat4 vpMatrix = glm::mat4(1.0f);
	glm::mat4 mvpMatrix = glm::mat4(1.0f);
};

static void render(VkQuake2Context& ctx)
{
	VkCommandBuffer cmd = ctx.m_defaultCommandBuffer->getHandle();
	VkExtent2D extent{ctx.width, ctx.height};
	const vulkan_setup_t& vulkan = ctx.m_vulkanSetup;

	vkWaitForFences(vulkan.device, 1, &ctx.frameFence, VK_TRUE, UINT64_MAX);
	vkResetFences(vulkan.device, 1, &ctx.frameFence);
	vkResetCommandBuffer(cmd, 0);

	ctx.m_defaultCommandBuffer->begin();

	// World pass transitions
	ctx.m_defaultCommandBuffer->imageMemoryBarrier(*ctx.worldColor.image,
		ctx.worldColor.image->m_imageLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	ctx.m_defaultCommandBuffer->imageMemoryBarrier(*ctx.worldDepth,
		ctx.worldDepth->m_imageLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_ACCESS_NONE, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

	ctx.m_defaultCommandBuffer->beginRenderPass(*ctx.worldPass, *ctx.worldFB);
	set_viewport_scissor(cmd, extent);

	// Lightmapped world polygon
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_world_lmap->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_base->getHandle(), ctx.ds_ubo_world->getHandle(), ctx.ds_lightmap->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_ubo_lmap_pc->getHandle(),
			0, 3, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_world->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);

		vkCmdPushConstants(cmd, ctx.layout_sampler_ubo_lmap_pc->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ctx.vpMatrix);
		vkCmdDraw(cmd, 6, 1, 0, 0);
	}

	// Water warp polygon
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_water->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_base->getHandle(), ctx.ds_ubo_water->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_ubo_pc->getHandle(),
			0, 2, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_water->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);

		vkCmdPushConstants(cmd, ctx.layout_sampler_ubo_pc->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ctx.vpMatrix);
		vkCmdDraw(cmd, 6, 1, 0, 0);
	}

	// Model
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_model->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_base->getHandle(), ctx.ds_ubo_model->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_ubo_pc->getHandle(),
			0, 2, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_model->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);

		vkCmdPushConstants(cmd, ctx.layout_sampler_ubo_pc->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ctx.vpMatrix);
		vkCmdDraw(cmd, 6, 1, 0, 0);
	}

	// Sprite
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_sprite->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_base->getHandle(), ctx.ds_ubo_sprite->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_ubo_pc->getHandle(),
			0, 2, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_sprite->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);

		vkCmdPushConstants(cmd, ctx.layout_sampler_ubo_pc->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ctx.mvpMatrix);
		vkCmdDraw(cmd, 6, 1, 0, 0);
	}

	// Particle
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_particle->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_base->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_pc->getHandle(),
			0, 1, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_particle->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);

		vkCmdPushConstants(cmd, ctx.layout_sampler_pc->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ctx.mvpMatrix);
		vkCmdDraw(cmd, 6, 1, 0, 0);
	}

	// Skybox
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_sky->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_sky->getHandle(), ctx.ds_ubo_sky->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_ubo_pc->getHandle(),
			0, 2, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_water->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);

		vkCmdPushConstants(cmd, ctx.layout_sampler_ubo_pc->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ctx.vpMatrix);
		vkCmdDraw(cmd, 6, 1, 0, 0);
	}

	// Beam
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_beam->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_ubo_beam->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_ubo_pc->getHandle(),
			0, 1, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_beam->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);

		vkCmdPushConstants(cmd, ctx.layout_ubo_pc->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ctx.mvpMatrix);
		vkCmdDraw(cmd, 4, 1, 0, 0);
	}

	// Dynamic light
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_dlight->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_ubo_dlight->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_ubo->getHandle(),
			0, 1, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_dlight->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);
		vkCmdDraw(cmd, 6, 1, 0, 0);
	}

	ctx.m_defaultCommandBuffer->endRenderPass();

	// Transition world -> shader read
	ctx.worldColor.image->m_imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ctx.m_defaultCommandBuffer->imageMemoryBarrier(*ctx.worldColor.image,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	// Warp pass
	ctx.m_defaultCommandBuffer->imageMemoryBarrier(*ctx.warpColor.image,
		ctx.warpColor.image->m_imageLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	ctx.m_defaultCommandBuffer->beginRenderPass(*ctx.warpPass, *ctx.warpFB);
	set_viewport_scissor(cmd, extent);
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_worldwarp->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_worldColor->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_frag_pc->getHandle(),
			0, 1, sets, 0, nullptr);

		float warp_pc[] = { 1.0f, 1.0f, static_cast<float>(extent.width), static_cast<float>(extent.height) };
		vkCmdPushConstants(cmd, ctx.layout_sampler_frag_pc->getHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(warp_pc), warp_pc);
		vkCmdDraw(cmd, 3, 1, 0, 0);
	}
	ctx.m_defaultCommandBuffer->endRenderPass();

	ctx.warpColor.image->m_imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ctx.m_defaultCommandBuffer->imageMemoryBarrier(*ctx.warpColor.image,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	// UI pass
	ctx.m_defaultCommandBuffer->imageMemoryBarrier(*ctx.uiColor.image,
		ctx.uiColor.image->m_imageLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	ctx.m_defaultCommandBuffer->beginRenderPass(*ctx.uiPass, *ctx.uiFB);
	set_viewport_scissor(cmd, extent);
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_postprocess->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_warpColor->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_frag_pc->getHandle(),
			0, 1, sets, 0, nullptr);

		float post_pc[] = { 1.0f, 1.0f };
		vkCmdPushConstants(cmd, ctx.layout_sampler_frag_pc->getHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(post_pc), post_pc);
		vkCmdDraw(cmd, 3, 1, 0, 0);
	}

	// UI textured quad
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_basic->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_ui->getHandle(), ctx.ds_ubo_basic->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_sampler_ubo->getHandle(),
			0, 2, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_ui->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);
		vkCmdBindIndexBuffer(cmd, ctx.ib_ui->getHandle(), 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
	}

	// UI color quad
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipe_colorquad->getHandle());
		VkDescriptorSet sets[] = { ctx.ds_ubo_colorquad->getHandle() };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout_ubo->getHandle(),
			0, 1, sets, 0, nullptr);

		VkBuffer vbo = ctx.vb_ui->getHandle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);
		vkCmdBindIndexBuffer(cmd, ctx.ib_ui->getHandle(), 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
	}

	ctx.m_defaultCommandBuffer->endRenderPass();

	ctx.m_defaultCommandBuffer->end();

	ctx.submit(ctx.m_defaultQueue, {ctx.m_defaultCommandBuffer}, ctx.frameFence, {}, {}, false);
	vkWaitForFences(vulkan.device, 1, &ctx.frameFence, VK_TRUE, UINT64_MAX);
}

int main(int argc, char** argv)
{
	auto ctx = std::make_unique<VkQuake2Context>();

	vulkan_req_t req;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME
	req.device_extensions.push_back(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME);
#endif
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
	req.device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_vkquake2", req);
	ctx->initBasic(vulkan, req);

	// Mock textures
	ctx->baseTex = create_texture(*ctx, vulkan.device,
		make_checker(64, 64, {200, 140, 100, 255}, {60, 30, 20, 255}),
		64, 64, VK_FORMAT_R8G8B8A8_UNORM);
	ctx->lightmapTex = create_texture(*ctx, vulkan.device,
		make_checker(64, 64, {200, 200, 200, 255}, {90, 90, 90, 255}, 4),
		64, 64, VK_FORMAT_R8G8B8A8_UNORM);
	ctx->skyTex = create_texture(*ctx, vulkan.device,
		make_gradient(64, 64, {40, 70, 120, 255}, {10, 20, 40, 255}),
		64, 64, VK_FORMAT_R8G8B8A8_UNORM);
	ctx->uiTex = create_texture(*ctx, vulkan.device,
		make_checker(32, 32, {255, 255, 255, 255}, {0, 0, 0, 255}, 2),
		32, 32, VK_FORMAT_R8G8B8A8_UNORM);

	// Render targets
	ctx->worldColor.image = std::make_shared<Image>(vulkan.device);
	ctx->worldColor.image->create({ctx->width, ctx->height, 1}, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->worldColor.view = std::make_shared<ImageView>(ctx->worldColor.image);
	ctx->worldColor.view->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	ctx->warpColor.image = std::make_shared<Image>(vulkan.device);
	ctx->warpColor.image->create({ctx->width, ctx->height, 1}, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->warpColor.view = std::make_shared<ImageView>(ctx->warpColor.image);
	ctx->warpColor.view->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	ctx->uiColor.image = std::make_shared<Image>(vulkan.device);
	ctx->uiColor.image->create({ctx->width, ctx->height, 1}, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->uiColor.view = std::make_shared<ImageView>(ctx->uiColor.image);
	ctx->uiColor.view->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	ctx->worldDepth = std::make_shared<Image>(vulkan.device);
	ctx->worldDepth->create({ctx->width, ctx->height, 1}, VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->worldDepthView = std::make_shared<ImageView>(ctx->worldDepth);
	ctx->worldDepthView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT);

	// Render passes
	{
		AttachmentInfo worldColor(0, ctx->worldColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		worldColor.m_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		worldColor.m_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		worldColor.m_description.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		worldColor.m_clear.color = { {0.1f, 0.1f, 0.2f, 1.0f} };

		AttachmentInfo worldDepth(1, ctx->worldDepthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		worldDepth.m_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		worldDepth.m_description.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		worldDepth.m_description.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		worldDepth.m_clear.depthStencil.depth = 1.0f;

		SubpassInfo subpass{};
		subpass.addColorAttachment(worldColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		subpass.setDepthStencilAttachment(worldDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

		ctx->worldPass = std::make_shared<RenderPass>(vulkan.device);
		ctx->worldPass->create({worldColor, worldDepth}, {subpass});
		ctx->worldFB = std::make_shared<FrameBuffer>(vulkan.device);
		ctx->worldFB->create(*ctx->worldPass, {ctx->width, ctx->height});
	}

	{
		AttachmentInfo warpColor(0, ctx->warpColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		warpColor.m_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		warpColor.m_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		warpColor.m_description.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		warpColor.m_clear.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

		SubpassInfo subpass{};
		subpass.addColorAttachment(warpColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		ctx->warpPass = std::make_shared<RenderPass>(vulkan.device);
		ctx->warpPass->create({warpColor}, {subpass});
		ctx->warpFB = std::make_shared<FrameBuffer>(vulkan.device);
		ctx->warpFB->create(*ctx->warpPass, {ctx->width, ctx->height});
	}

	{
		AttachmentInfo uiColor(0, ctx->uiColor.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		uiColor.m_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		uiColor.m_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		uiColor.m_description.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		uiColor.m_clear.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

		SubpassInfo subpass{};
		subpass.addColorAttachment(uiColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		ctx->uiPass = std::make_shared<RenderPass>(vulkan.device);
		ctx->uiPass->create({uiColor}, {subpass});
		ctx->uiFB = std::make_shared<FrameBuffer>(vulkan.device);
		ctx->uiFB->create(*ctx->uiPass, {ctx->width, ctx->height});

		ctx->m_renderPass = ctx->uiPass;
		ctx->m_framebuffer = ctx->uiFB;
	}

	// Sampler
	ctx->sampler = std::make_unique<Sampler>(vulkan.device);
	ctx->sampler->create(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
		req.samplerAnisotropy, vulkan.device_properties.limits.maxSamplerAnisotropy);

	// Descriptor layouts and pools
	ctx->samplerLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	ctx->samplerLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	ctx->samplerLayout->create();

	ctx->uboLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	ctx->uboLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS);
	ctx->uboLayout->create();

	ctx->samplerPool = std::make_shared<DescriptorSetPool>(ctx->samplerLayout);
	ctx->samplerPool->create(16);
	ctx->uboPool = std::make_shared<DescriptorSetPool>(ctx->uboLayout);
	ctx->uboPool->create(16);

	// Descriptor sets for textures
	ctx->ds_base = std::make_unique<DescriptorSet>(ctx->samplerPool);
	ctx->ds_base->create();
	ctx->ds_base->setCombinedImageSampler(0, *ctx->baseTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *ctx->sampler);
	ctx->ds_base->update();

	ctx->ds_lightmap = std::make_unique<DescriptorSet>(ctx->samplerPool);
	ctx->ds_lightmap->create();
	ctx->ds_lightmap->setCombinedImageSampler(0, *ctx->lightmapTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *ctx->sampler);
	ctx->ds_lightmap->update();

	ctx->ds_sky = std::make_unique<DescriptorSet>(ctx->samplerPool);
	ctx->ds_sky->create();
	ctx->ds_sky->setCombinedImageSampler(0, *ctx->skyTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *ctx->sampler);
	ctx->ds_sky->update();

	ctx->ds_ui = std::make_unique<DescriptorSet>(ctx->samplerPool);
	ctx->ds_ui->create();
	ctx->ds_ui->setCombinedImageSampler(0, *ctx->uiTex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *ctx->sampler);
	ctx->ds_ui->update();

	ctx->ds_worldColor = std::make_unique<DescriptorSet>(ctx->samplerPool);
	ctx->ds_worldColor->create();
	ctx->ds_worldColor->setCombinedImageSampler(0, *ctx->worldColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *ctx->sampler);
	ctx->ds_worldColor->update();

	ctx->ds_warpColor = std::make_unique<DescriptorSet>(ctx->samplerPool);
	ctx->ds_warpColor->create();
	ctx->ds_warpColor->setCombinedImageSampler(0, *ctx->warpColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *ctx->sampler);
	ctx->ds_warpColor->update();

	// Vertex buffers
	const std::array<VertexPosUVLmap, 6> worldVerts = {{
		{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}},
		{{ 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}},
		{{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}},
		{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}},
		{{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}},
		{{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}},
	}};

	const std::array<VertexPosUV, 6> quadVerts = {{
		{{-0.7f, -0.7f, 0.2f}, {0.0f, 0.0f}},
		{{ 0.7f, -0.7f, 0.2f}, {1.0f, 0.0f}},
		{{ 0.7f,  0.7f, 0.2f}, {1.0f, 1.0f}},
		{{-0.7f, -0.7f, 0.2f}, {0.0f, 0.0f}},
		{{ 0.7f,  0.7f, 0.2f}, {1.0f, 1.0f}},
		{{-0.7f,  0.7f, 0.2f}, {0.0f, 1.0f}},
	}};

	const std::array<VertexPosColorUV, 6> modelVerts = {{
		{{-0.5f, -0.2f, 0.5f}, {1.0f, 0.2f, 0.2f, 1.0f}, {0.0f, 0.0f}},
		{{ 0.5f, -0.2f, 0.5f}, {0.2f, 1.0f, 0.2f, 1.0f}, {1.0f, 0.0f}},
		{{ 0.5f,  0.6f, 0.5f}, {0.2f, 0.2f, 1.0f, 1.0f}, {1.0f, 1.0f}},
		{{-0.5f, -0.2f, 0.5f}, {1.0f, 0.2f, 0.2f, 1.0f}, {0.0f, 0.0f}},
		{{ 0.5f,  0.6f, 0.5f}, {0.2f, 0.2f, 1.0f, 1.0f}, {1.0f, 1.0f}},
		{{-0.5f,  0.6f, 0.5f}, {0.8f, 0.8f, 0.2f, 1.0f}, {0.0f, 1.0f}},
	}};

	const std::array<VertexPosColorUV, 6> particleVerts = {{
		{{-0.2f, -0.8f, 0.3f}, {1.0f, 0.5f, 0.2f, 1.0f}, {0.0f, 0.0f}},
		{{ 0.2f, -0.8f, 0.3f}, {1.0f, 0.5f, 0.2f, 1.0f}, {1.0f, 0.0f}},
		{{ 0.2f, -0.4f, 0.3f}, {1.0f, 0.5f, 0.2f, 1.0f}, {1.0f, 1.0f}},
		{{-0.2f, -0.8f, 0.3f}, {1.0f, 0.5f, 0.2f, 1.0f}, {0.0f, 0.0f}},
		{{ 0.2f, -0.4f, 0.3f}, {1.0f, 0.5f, 0.2f, 1.0f}, {1.0f, 1.0f}},
		{{-0.2f, -0.4f, 0.3f}, {1.0f, 0.5f, 0.2f, 1.0f}, {0.0f, 1.0f}},
	}};

	const std::array<VertexPos, 4> beamVerts = {{
		{{-0.9f, -0.1f, 0.4f}},
		{{-0.9f,  0.1f, 0.4f}},
		{{ 0.9f, -0.1f, 0.4f}},
		{{ 0.9f,  0.1f, 0.4f}},
	}};

	const std::array<VertexPosColor, 6> dlightVerts = {{
		{{-0.2f, -0.2f, 0.7f}, {1.0f, 0.5f, 0.1f}},
		{{ 0.2f, -0.2f, 0.7f}, {1.0f, 0.5f, 0.1f}},
		{{ 0.2f,  0.2f, 0.7f}, {1.0f, 0.5f, 0.1f}},
		{{-0.2f, -0.2f, 0.7f}, {1.0f, 0.5f, 0.1f}},
		{{ 0.2f,  0.2f, 0.7f}, {1.0f, 0.5f, 0.1f}},
		{{-0.2f,  0.2f, 0.7f}, {1.0f, 0.5f, 0.1f}},
	}};

	const std::array<VertexUI, 4> uiVerts = {{
		{{-1.0f, -1.0f}, {0.0f, 0.0f}},
		{{ 1.0f,  1.0f}, {1.0f, 1.0f}},
		{{-1.0f,  1.0f}, {0.0f, 1.0f}},
		{{ 1.0f, -1.0f}, {1.0f, 0.0f}},
	}};

	const std::array<uint16_t, 6> uiIndices = {{0, 1, 2, 0, 3, 1}};

	ctx->vb_world = std::make_unique<Buffer>(vulkan);
	ctx->vb_world->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		worldVerts.size() * sizeof(VertexPosUVLmap), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(worldVerts.data()), sizeof(worldVerts), *ctx->vb_world);

	ctx->vb_water = std::make_unique<Buffer>(vulkan);
	ctx->vb_water->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		quadVerts.size() * sizeof(VertexPosUV), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(quadVerts.data()), sizeof(quadVerts), *ctx->vb_water);

	ctx->vb_model = std::make_unique<Buffer>(vulkan);
	ctx->vb_model->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		modelVerts.size() * sizeof(VertexPosColorUV), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(modelVerts.data()), sizeof(modelVerts), *ctx->vb_model);

	ctx->vb_sprite = std::make_unique<Buffer>(vulkan);
	ctx->vb_sprite->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		quadVerts.size() * sizeof(VertexPosUV), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(quadVerts.data()), sizeof(quadVerts), *ctx->vb_sprite);

	ctx->vb_particle = std::make_unique<Buffer>(vulkan);
	ctx->vb_particle->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		particleVerts.size() * sizeof(VertexPosColorUV), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(particleVerts.data()), sizeof(particleVerts), *ctx->vb_particle);

	ctx->vb_beam = std::make_unique<Buffer>(vulkan);
	ctx->vb_beam->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		beamVerts.size() * sizeof(VertexPos), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(beamVerts.data()), sizeof(beamVerts), *ctx->vb_beam);

	ctx->vb_dlight = std::make_unique<Buffer>(vulkan);
	ctx->vb_dlight->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		dlightVerts.size() * sizeof(VertexPosColor), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(dlightVerts.data()), sizeof(dlightVerts), *ctx->vb_dlight);

	ctx->vb_ui = std::make_unique<Buffer>(vulkan);
	ctx->vb_ui->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		uiVerts.size() * sizeof(VertexUI), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(uiVerts.data()), sizeof(uiVerts), *ctx->vb_ui);

	ctx->ib_ui = std::make_unique<Buffer>(vulkan);
	ctx->ib_ui->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		uiIndices.size() * sizeof(uint16_t), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ctx->updateBuffer(reinterpret_cast<const char*>(uiIndices.data()), sizeof(uiIndices), *ctx->ib_ui);

	// UBOs
	ctx->ubo_world = std::make_unique<Buffer>(vulkan);
	ctx->ubo_world->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboLmap),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_world->map();

	ctx->ubo_water = std::make_unique<Buffer>(vulkan);
	ctx->ubo_water->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboWarp),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_water->map();

	ctx->ubo_model = std::make_unique<Buffer>(vulkan);
	ctx->ubo_model->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboModel),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_model->map();

	ctx->ubo_sprite = std::make_unique<Buffer>(vulkan);
	ctx->ubo_sprite->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboSprite),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_sprite->map();

	ctx->ubo_sky = std::make_unique<Buffer>(vulkan);
	ctx->ubo_sky->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboSky),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_sky->map();

	ctx->ubo_basic = std::make_unique<Buffer>(vulkan);
	ctx->ubo_basic->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboImageTransform),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_basic->map();

	ctx->ubo_beam = std::make_unique<Buffer>(vulkan);
	ctx->ubo_beam->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboBeam),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_beam->map();

	ctx->ubo_dlight = std::make_unique<Buffer>(vulkan);
	ctx->ubo_dlight->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboDLight),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_dlight->map();

	ctx->ubo_colorquad = std::make_unique<Buffer>(vulkan);
	ctx->ubo_colorquad->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UboColorQuad),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	ctx->ubo_colorquad->map();

	// Descriptor sets for UBOs
	ctx->ds_ubo_world = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_world->create();
	ctx->ds_ubo_world->setBuffer(0, *ctx->ubo_world);
	ctx->ds_ubo_world->update();

	ctx->ds_ubo_water = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_water->create();
	ctx->ds_ubo_water->setBuffer(0, *ctx->ubo_water);
	ctx->ds_ubo_water->update();

	ctx->ds_ubo_model = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_model->create();
	ctx->ds_ubo_model->setBuffer(0, *ctx->ubo_model);
	ctx->ds_ubo_model->update();

	ctx->ds_ubo_sprite = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_sprite->create();
	ctx->ds_ubo_sprite->setBuffer(0, *ctx->ubo_sprite);
	ctx->ds_ubo_sprite->update();

	ctx->ds_ubo_sky = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_sky->create();
	ctx->ds_ubo_sky->setBuffer(0, *ctx->ubo_sky);
	ctx->ds_ubo_sky->update();

	ctx->ds_ubo_basic = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_basic->create();
	ctx->ds_ubo_basic->setBuffer(0, *ctx->ubo_basic);
	ctx->ds_ubo_basic->update();

	ctx->ds_ubo_beam = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_beam->create();
	ctx->ds_ubo_beam->setBuffer(0, *ctx->ubo_beam);
	ctx->ds_ubo_beam->update();

	ctx->ds_ubo_dlight = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_dlight->create();
	ctx->ds_ubo_dlight->setBuffer(0, *ctx->ubo_dlight);
	ctx->ds_ubo_dlight->update();

	ctx->ds_ubo_colorquad = std::make_unique<DescriptorSet>(ctx->uboPool);
	ctx->ds_ubo_colorquad->create();
	ctx->ds_ubo_colorquad->setBuffer(0, *ctx->ubo_colorquad);
	ctx->ds_ubo_colorquad->update();

	// Fill UBO data
	{
		glm::mat4 proj = glm::perspective(glm::radians(60.0f), ctx->width / (float)ctx->height, 0.1f, 100.0f);
		proj[1][1] *= -1.0f;
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 vp = proj * view;
		ctx->vpMatrix = vp;
		ctx->mvpMatrix = vp;

		UboLmap lmap{};
		lmap.model = glm::mat4(1.0f);
		lmap.viewLightmaps = 0.0f;
		memcpy(ctx->ubo_world->m_mappedAddress, &lmap, sizeof(lmap));
		ctx->ubo_world->flush(true);

		UboWarp warp{};
		warp.model = glm::mat4(1.0f);
		warp.color = glm::vec4(0.6f, 0.7f, 1.0f, 0.8f);
		warp.time = 0.5f;
		warp.scroll = 0.1f;
		memcpy(ctx->ubo_water->m_mappedAddress, &warp, sizeof(warp));
		ctx->ubo_water->flush(true);

		UboModel model{};
		model.model = glm::mat4(1.0f);
		model.textured = 1;
		memcpy(ctx->ubo_model->m_mappedAddress, &model, sizeof(model));
		ctx->ubo_model->flush(true);

		UboSprite sprite{};
		sprite.alpha = 0.8f;
		memcpy(ctx->ubo_sprite->m_mappedAddress, &sprite, sizeof(sprite));
		ctx->ubo_sprite->flush(true);

		UboSky sky{};
		sky.model = glm::scale(glm::mat4(1.0f), glm::vec3(3.0f));
		memcpy(ctx->ubo_sky->m_mappedAddress, &sky, sizeof(sky));
		ctx->ubo_sky->flush(true);

		UboImageTransform ui{};
		ui.offset = glm::vec2(0.6f, 0.6f);
		ui.scale = glm::vec2(0.25f, 0.25f);
		ui.uvOffset = glm::vec2(0.0f, 0.0f);
		ui.uvScale = glm::vec2(1.0f, 1.0f);
		memcpy(ctx->ubo_basic->m_mappedAddress, &ui, sizeof(ui));
		ctx->ubo_basic->flush(true);

		UboBeam beam{};
		beam.color = glm::vec4(0.2f, 1.0f, 0.9f, 0.6f);
		memcpy(ctx->ubo_beam->m_mappedAddress, &beam, sizeof(beam));
		ctx->ubo_beam->flush(true);

		UboDLight dlight{};
		dlight.mvp = vp;
		memcpy(ctx->ubo_dlight->m_mappedAddress, &dlight, sizeof(dlight));
		ctx->ubo_dlight->flush(true);

		UboColorQuad colorquad{};
		colorquad.offset = glm::vec2(-0.6f, -0.6f);
		colorquad.scale = glm::vec2(0.2f, 0.2f);
		colorquad.color = glm::vec4(1.0f, 0.1f, 0.1f, 0.6f);
		memcpy(ctx->ubo_colorquad->m_mappedAddress, &colorquad, sizeof(colorquad));
		ctx->ubo_colorquad->flush(true);
	}

	// Pipeline layouts
	{
		VkPushConstantRange pc_mat{};
		pc_mat.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pc_mat.offset = 0;
		pc_mat.size = sizeof(glm::mat4);

		VkPushConstantRange pc_frag{};
		pc_frag.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pc_frag.offset = 0;
		pc_frag.size = sizeof(float) * 4;

		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> sampler_ubo = {{0, ctx->samplerLayout}, {1, ctx->uboLayout}};
		ctx->layout_sampler_ubo_pc = std::make_shared<PipelineLayout>(vulkan.device);
		ctx->layout_sampler_ubo_pc->create(sampler_ubo, {pc_mat});

		ctx->layout_sampler_ubo = std::make_shared<PipelineLayout>(vulkan.device);
		ctx->layout_sampler_ubo->create(sampler_ubo);

		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> sampler_ubo_lmap = {{0, ctx->samplerLayout}, {1, ctx->uboLayout}, {2, ctx->samplerLayout}};
		ctx->layout_sampler_ubo_lmap_pc = std::make_shared<PipelineLayout>(vulkan.device);
		ctx->layout_sampler_ubo_lmap_pc->create(sampler_ubo_lmap, {pc_mat});

		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> sampler_only = {{0, ctx->samplerLayout}};
		ctx->layout_sampler_pc = std::make_shared<PipelineLayout>(vulkan.device);
		ctx->layout_sampler_pc->create(sampler_only, {pc_mat});

		ctx->layout_sampler_frag_pc = std::make_shared<PipelineLayout>(vulkan.device);
		ctx->layout_sampler_frag_pc->create(sampler_only, {pc_frag});

		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> ubo_only = {{0, ctx->uboLayout}};
		ctx->layout_ubo_pc = std::make_shared<PipelineLayout>(vulkan.device);
		ctx->layout_ubo_pc->create(ubo_only, {pc_mat});

		ctx->layout_ubo = std::make_shared<PipelineLayout>(vulkan.device);
		ctx->layout_ubo->create(ubo_only);
	}

	// Shaders
	auto sh_basic_vert = std::make_shared<Shader>(vulkan.device);
	sh_basic_vert->create(reinterpret_cast<const unsigned char*>(basic_vert_spv), sizeof(basic_vert_spv));
	auto sh_basic_frag = std::make_shared<Shader>(vulkan.device);
	sh_basic_frag->create(reinterpret_cast<const unsigned char*>(basic_frag_spv), sizeof(basic_frag_spv));

	auto sh_basic_color_vert = std::make_shared<Shader>(vulkan.device);
	sh_basic_color_vert->create(reinterpret_cast<const unsigned char*>(basic_color_quad_vert_spv), sizeof(basic_color_quad_vert_spv));
	auto sh_basic_color_frag = std::make_shared<Shader>(vulkan.device);
	sh_basic_color_frag->create(reinterpret_cast<const unsigned char*>(basic_color_quad_frag_spv), sizeof(basic_color_quad_frag_spv));

	auto sh_model_vert = std::make_shared<Shader>(vulkan.device);
	sh_model_vert->create(reinterpret_cast<const unsigned char*>(model_vert_spv), sizeof(model_vert_spv));
	auto sh_model_frag = std::make_shared<Shader>(vulkan.device);
	sh_model_frag->create(reinterpret_cast<const unsigned char*>(model_frag_spv), sizeof(model_frag_spv));

	auto sh_particle_vert = std::make_shared<Shader>(vulkan.device);
	sh_particle_vert->create(reinterpret_cast<const unsigned char*>(particle_vert_spv), sizeof(particle_vert_spv));

	auto sh_sprite_vert = std::make_shared<Shader>(vulkan.device);
	sh_sprite_vert->create(reinterpret_cast<const unsigned char*>(sprite_vert_spv), sizeof(sprite_vert_spv));

	auto sh_beam_vert = std::make_shared<Shader>(vulkan.device);
	sh_beam_vert->create(reinterpret_cast<const unsigned char*>(beam_vert_spv), sizeof(beam_vert_spv));

	auto sh_sky_vert = std::make_shared<Shader>(vulkan.device);
	sh_sky_vert->create(reinterpret_cast<const unsigned char*>(skybox_vert_spv), sizeof(skybox_vert_spv));

	auto sh_dlight_vert = std::make_shared<Shader>(vulkan.device);
	sh_dlight_vert->create(reinterpret_cast<const unsigned char*>(d_light_vert_spv), sizeof(d_light_vert_spv));

	auto sh_lmap_vert = std::make_shared<Shader>(vulkan.device);
	sh_lmap_vert->create(reinterpret_cast<const unsigned char*>(polygon_lmap_vert_spv), sizeof(polygon_lmap_vert_spv));
	auto sh_lmap_frag = std::make_shared<Shader>(vulkan.device);
	sh_lmap_frag->create(reinterpret_cast<const unsigned char*>(polygon_lmap_frag_spv), sizeof(polygon_lmap_frag_spv));

	auto sh_warp_vert = std::make_shared<Shader>(vulkan.device);
	sh_warp_vert->create(reinterpret_cast<const unsigned char*>(polygon_warp_vert_spv), sizeof(polygon_warp_vert_spv));

	auto sh_worldwarp_vert = std::make_shared<Shader>(vulkan.device);
	sh_worldwarp_vert->create(reinterpret_cast<const unsigned char*>(world_warp_vert_spv), sizeof(world_warp_vert_spv));
	auto sh_worldwarp_frag = std::make_shared<Shader>(vulkan.device);
	sh_worldwarp_frag->create(reinterpret_cast<const unsigned char*>(world_warp_frag_spv), sizeof(world_warp_frag_spv));

	auto sh_post_vert = std::make_shared<Shader>(vulkan.device);
	sh_post_vert->create(reinterpret_cast<const unsigned char*>(postprocess_vert_spv), sizeof(postprocess_vert_spv));
	auto sh_post_frag = std::make_shared<Shader>(vulkan.device);
	sh_post_frag->create(reinterpret_cast<const unsigned char*>(postprocess_frag_spv), sizeof(postprocess_frag_spv));

	// Pipelines
	{
		GraphicPipelineState lmapState;
		lmapState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		lmapState.setVertexBinding(0, *ctx->vb_world, sizeof(VertexPosUVLmap));
		lmapState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosUVLmap, pos));
		lmapState.setVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosUVLmap, uv));
		lmapState.setVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosUVLmap, lmap));
		lmapState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		lmapState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		lmapState.setColorBlendAttachment(0, blend_state(false));

		ShaderPipelineState lmapVS(VK_SHADER_STAGE_VERTEX_BIT, sh_lmap_vert);
		ShaderPipelineState lmapFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_lmap_frag);
		ctx->pipe_world_lmap = std::make_unique<GraphicPipeline>(ctx->layout_sampler_ubo_lmap_pc);
		ctx->pipe_world_lmap->create({lmapVS, lmapFS}, lmapState, *ctx->worldPass);
	}

	{
		GraphicPipelineState waterState;
		waterState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		waterState.setVertexBinding(0, *ctx->vb_water, sizeof(VertexPosUV));
		waterState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosUV, pos));
		waterState.setVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosUV, uv));
		waterState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		waterState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		waterState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		waterState.setColorBlendAttachment(0, blend_state(true));

		ShaderPipelineState waterVS(VK_SHADER_STAGE_VERTEX_BIT, sh_warp_vert);
		ShaderPipelineState waterFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_basic_frag);
		ctx->pipe_water = std::make_unique<GraphicPipeline>(ctx->layout_sampler_ubo_pc);
		ctx->pipe_water->create({waterVS, waterFS}, waterState, *ctx->worldPass);
	}

	{
		GraphicPipelineState modelState;
		modelState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		modelState.setVertexBinding(0, *ctx->vb_model, sizeof(VertexPosColorUV));
		modelState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosColorUV, pos));
		modelState.setVertexAttribute(1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexPosColorUV, color));
		modelState.setVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosColorUV, uv));
		modelState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		modelState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		modelState.setColorBlendAttachment(0, blend_state(true));

		ShaderPipelineState modelVS(VK_SHADER_STAGE_VERTEX_BIT, sh_model_vert);
		ShaderPipelineState modelFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_model_frag);
		ctx->pipe_model = std::make_unique<GraphicPipeline>(ctx->layout_sampler_ubo_pc);
		ctx->pipe_model->create({modelVS, modelFS}, modelState, *ctx->worldPass);
	}

	{
		GraphicPipelineState spriteState;
		spriteState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		spriteState.setVertexBinding(0, *ctx->vb_sprite, sizeof(VertexPosUV));
		spriteState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosUV, pos));
		spriteState.setVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosUV, uv));
		spriteState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		spriteState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		spriteState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		spriteState.setColorBlendAttachment(0, blend_state(true));

		ShaderPipelineState spriteVS(VK_SHADER_STAGE_VERTEX_BIT, sh_sprite_vert);
		ShaderPipelineState spriteFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_basic_frag);
		ctx->pipe_sprite = std::make_unique<GraphicPipeline>(ctx->layout_sampler_ubo_pc);
		ctx->pipe_sprite->create({spriteVS, spriteFS}, spriteState, *ctx->worldPass);
	}

	{
		GraphicPipelineState particleState;
		particleState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		particleState.setVertexBinding(0, *ctx->vb_particle, sizeof(VertexPosColorUV));
		particleState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosColorUV, pos));
		particleState.setVertexAttribute(1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexPosColorUV, color));
		particleState.setVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosColorUV, uv));
		particleState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		particleState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		particleState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		particleState.setColorBlendAttachment(0, blend_state(true));

		ShaderPipelineState particleVS(VK_SHADER_STAGE_VERTEX_BIT, sh_particle_vert);
		ShaderPipelineState particleFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_basic_frag);
		ctx->pipe_particle = std::make_unique<GraphicPipeline>(ctx->layout_sampler_pc);
		ctx->pipe_particle->create({particleVS, particleFS}, particleState, *ctx->worldPass);
	}

	{
		GraphicPipelineState skyState;
		skyState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		skyState.setVertexBinding(0, *ctx->vb_water, sizeof(VertexPosUV));
		skyState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosUV, pos));
		skyState.setVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPosUV, uv));
		skyState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		skyState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		skyState.m_depthStencilState.depthTestEnable = VK_FALSE;
		skyState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		skyState.setColorBlendAttachment(0, blend_state(false));

		ShaderPipelineState skyVS(VK_SHADER_STAGE_VERTEX_BIT, sh_sky_vert);
		ShaderPipelineState skyFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_basic_frag);
		ctx->pipe_sky = std::make_unique<GraphicPipeline>(ctx->layout_sampler_ubo_pc);
		ctx->pipe_sky->create({skyVS, skyFS}, skyState, *ctx->worldPass);
	}

	{
		GraphicPipelineState beamState;
		beamState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		beamState.setVertexBinding(0, *ctx->vb_beam, sizeof(VertexPos));
		beamState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPos, pos));
		beamState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		beamState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		beamState.m_inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		beamState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		beamState.setColorBlendAttachment(0, blend_state(true));

		ShaderPipelineState beamVS(VK_SHADER_STAGE_VERTEX_BIT, sh_beam_vert);
		ShaderPipelineState beamFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_basic_color_frag);
		ctx->pipe_beam = std::make_unique<GraphicPipeline>(ctx->layout_ubo_pc);
		ctx->pipe_beam->create({beamVS, beamFS}, beamState, *ctx->worldPass);
	}

	{
		GraphicPipelineState dlightState;
		dlightState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		dlightState.setVertexBinding(0, *ctx->vb_dlight, sizeof(VertexPosColor));
		dlightState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosColor, pos));
		dlightState.setVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPosColor, color));
		dlightState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		dlightState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		dlightState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		dlightState.setColorBlendAttachment(0, blend_state(true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE));

		ShaderPipelineState dlightVS(VK_SHADER_STAGE_VERTEX_BIT, sh_dlight_vert);
		ShaderPipelineState dlightFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_basic_color_frag);
		ctx->pipe_dlight = std::make_unique<GraphicPipeline>(ctx->layout_ubo);
		ctx->pipe_dlight->create({dlightVS, dlightFS}, dlightState, *ctx->worldPass);
	}

	{
		GraphicPipelineState warpState;
		warpState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		warpState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		warpState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		warpState.m_depthStencilState.depthTestEnable = VK_FALSE;
		warpState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		warpState.setColorBlendAttachment(0, blend_state(false));

		ShaderPipelineState warpVS(VK_SHADER_STAGE_VERTEX_BIT, sh_worldwarp_vert);
		ShaderPipelineState warpFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_worldwarp_frag);
		ctx->pipe_worldwarp = std::make_unique<GraphicPipeline>(ctx->layout_sampler_frag_pc);
		ctx->pipe_worldwarp->create({warpVS, warpFS}, warpState, *ctx->warpPass);
	}

	{
		GraphicPipelineState postState;
		postState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		postState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		postState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		postState.m_depthStencilState.depthTestEnable = VK_FALSE;
		postState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		postState.setColorBlendAttachment(0, blend_state(false));

		ShaderPipelineState postVS(VK_SHADER_STAGE_VERTEX_BIT, sh_post_vert);
		ShaderPipelineState postFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_post_frag);
		ctx->pipe_postprocess = std::make_unique<GraphicPipeline>(ctx->layout_sampler_frag_pc);
		ctx->pipe_postprocess->create({postVS, postFS}, postState, *ctx->uiPass);
	}

	{
		GraphicPipelineState basicState;
		basicState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		basicState.setVertexBinding(0, *ctx->vb_ui, sizeof(VertexUI));
		basicState.setVertexAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexUI, pos));
		basicState.setVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexUI, uv));
		basicState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		basicState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		basicState.m_depthStencilState.depthTestEnable = VK_FALSE;
		basicState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		basicState.setColorBlendAttachment(0, blend_state(true));

		ShaderPipelineState basicVS(VK_SHADER_STAGE_VERTEX_BIT, sh_basic_vert);
		ShaderPipelineState basicFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_basic_frag);
		ctx->pipe_basic = std::make_unique<GraphicPipeline>(ctx->layout_sampler_ubo);
		ctx->pipe_basic->create({basicVS, basicFS}, basicState, *ctx->uiPass);
	}

	{
		GraphicPipelineState colorState;
		colorState.m_rasterizationState.cullMode = VK_CULL_MODE_NONE;
		colorState.setVertexBinding(0, *ctx->vb_ui, sizeof(VertexUI));
		colorState.setVertexAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexUI, pos));
		colorState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
		colorState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);
		colorState.m_depthStencilState.depthTestEnable = VK_FALSE;
		colorState.m_depthStencilState.depthWriteEnable = VK_FALSE;
		colorState.setColorBlendAttachment(0, blend_state(true));

		ShaderPipelineState colorVS(VK_SHADER_STAGE_VERTEX_BIT, sh_basic_color_vert);
		ShaderPipelineState colorFS(VK_SHADER_STAGE_FRAGMENT_BIT, sh_basic_color_frag);
		ctx->pipe_colorquad = std::make_unique<GraphicPipeline>(ctx->layout_ubo);
		ctx->pipe_colorquad->create({colorVS, colorFS}, colorState, *ctx->uiPass);
	}

	// Release local shader refs (pipelines hold the shared_ptrs).
	sh_basic_vert = nullptr;
	sh_basic_frag = nullptr;
	sh_basic_color_vert = nullptr;
	sh_basic_color_frag = nullptr;
	sh_model_vert = nullptr;
	sh_model_frag = nullptr;
	sh_particle_vert = nullptr;
	sh_sprite_vert = nullptr;
	sh_beam_vert = nullptr;
	sh_sky_vert = nullptr;
	sh_dlight_vert = nullptr;
	sh_lmap_vert = nullptr;
	sh_lmap_frag = nullptr;
	sh_warp_vert = nullptr;
	sh_worldwarp_vert = nullptr;
	sh_worldwarp_frag = nullptr;
	sh_post_vert = nullptr;
	sh_post_frag = nullptr;

	// Submit staging uploads
	ctx->submitStaging(true, {}, {}, false);

	// Fence
	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	check(vkCreateFence(vulkan.device, &fenceInfo, nullptr, &ctx->frameFence));

	bool first_loop = true;
	benchmarking bench = vulkan.bench;
	while (p__loops--)
	{
		if (!first_loop)
		{
			bench_stop_iteration(bench);
		}
		bench_start_iteration(bench);
		render(*ctx);
		first_loop = false;
	}
	bench_stop_iteration(bench);

	ctx->saveImageOutput();
	vkDeviceWaitIdle(vulkan.device);
	ctx = nullptr;
	return 0;
}
