// LLM-driven Ping-Pong game microbenchmark using LiteRT-LM and Vulkan rendering.

#include "vulkan_common.h"
#include "litert_common.h"
#include "vulkan_llm_pingpong_vert.inc"
#include "vulkan_llm_pingpong_frag.inc"

#include <c/engine.h>

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <cctype>
#include <cassert>

struct Vertex
{
	float x, y;
	float r, g, b, a;
};

static std::string model_path_arg;
static std::string backend_arg = "gpu";
static int total_frames = 60;
static bool save_image_output = false;
static bool save_all_frames = false;
static int target_save_frame = -1;
static std::string image_filename = "llm_pingpong.png";
static int seed_arg = -1;
static float temperature_arg = 1.0f;

static void show_usage()
{
	printf("-i/--image-output [N|file] Save rendered image (N = frame index, or filename, default: final frame)\n");
	printf("-iall/--image-all          Save all rendered frames as llm_pingpong_%%04d.png\n");
	printf("-m/--model <path>         Path to .litertlm model file\n");
	printf("-b/--backend <cpu|gpu>    LiteRT-LM backend (default 'gpu')\n");
	printf("-f/--frames <N>           Number of frames to render (default 60)\n");
	printf("-s/--seed <N>             Random seed for LLM sampling (default: time-based)\n");
	printf("-t/--temperature <val>    Sampling temperature (default 1.0)\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-iall", "--image-all"))
	{
		save_all_frames = true;
		save_image_output = true;
		return true;
	}
	if (match(argv[i], "-i", "--image-output"))
	{
		save_image_output = true;
		if (i + 1 < argc && argv[i + 1][0] != '-')
		{
			const char* val = argv[++i];
			char* end = nullptr;
			long frame_num = strtol(val, &end, 10);
			if (end && *end == '\0')
			{
				target_save_frame = static_cast<int>(frame_num);
			}
			else
			{
				image_filename = val;
			}
		}
		return true;
	}
	if (match(argv[i], "-m", "--model"))
	{
		model_path_arg = get_string_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-b", "--backend"))
	{
		backend_arg = get_string_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-f", "--frames"))
	{
		total_frames = get_arg(argv, ++i, argc);
		assert(total_frames > 0);
		return true;
	}
	if (match(argv[i], "-s", "--seed"))
	{
		seed_arg = get_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-t", "--temperature"))
	{
		temperature_arg = static_cast<float>(atof(get_string_arg(argv, ++i, argc)));
		return true;
	}
	return false;
}

static void hsv_to_rgb(float h, float s, float v, float out_rgb[3])
{
	float c = v * s;
	float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
	float m = v - c;
	float r = 0.0f, g = 0.0f, b = 0.0f;
	if (h < 60.0f) { r = c; g = x; b = 0.0f; }
	else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
	else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
	else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
	else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
	else { r = c; g = 0.0f; b = x; }
	out_rgb[0] = r + m;
	out_rgb[1] = g + m;
	out_rgb[2] = b + m;
}

static unsigned int string_hash(const std::string& str)
{
	unsigned int h = 2166136261u;
	for (char c : str)
	{
		h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
	}
	return h;
}

static bool parse_hex_color(const std::string& hex, float& r, float& g, float& b)
{
	size_t start = hex.find('#');
	if (start == std::string::npos || hex.size() < start + 7)
	{
		return false;
	}
	unsigned int val = 0;
	if (sscanf(hex.c_str() + start + 1, "%06x", &val) != 1)
	{
		return false;
	}
	r = ((val >> 16) & 0xFF) / 255.0f;
	g = ((val >> 8) & 0xFF) / 255.0f;
	b = (val & 0xFF) / 255.0f;
	return true;
}

static void add_quad(std::vector<Vertex>& verts, float x0, float y0, float x1, float y1,
                     const float c0[4], const float c1[4], const float c2[4], const float c3[4])
{
	verts.push_back({ x0, y0, c0[0], c0[1], c0[2], c0[3] });
	verts.push_back({ x1, y0, c1[0], c1[1], c1[2], c1[3] });
	verts.push_back({ x1, y1, c2[0], c2[1], c2[2], c2[3] });

	verts.push_back({ x0, y0, c0[0], c0[1], c0[2], c0[3] });
	verts.push_back({ x1, y1, c2[0], c2[1], c2[2], c2[3] });
	verts.push_back({ x0, y1, c3[0], c3[1], c3[2], c3[3] });
}

static void add_trapezoid(std::vector<Vertex>& verts,
                          float x0_l, float x0_r, float y0,
                          float x1_l, float x1_r, float y1,
                          const float c0_l[4], const float c0_r[4],
                          const float c1_l[4], const float c1_r[4])
{
	verts.push_back({ x0_l, y0, c0_l[0], c0_l[1], c0_l[2], c0_l[3] });
	verts.push_back({ x0_r, y0, c0_r[0], c0_r[1], c0_r[2], c0_r[3] });
	verts.push_back({ x1_r, y1, c1_r[0], c1_r[1], c1_r[2], c1_r[3] });

	verts.push_back({ x0_l, y0, c0_l[0], c0_l[1], c0_l[2], c0_l[3] });
	verts.push_back({ x1_r, y1, c1_r[0], c1_r[1], c1_r[2], c1_r[3] });
	verts.push_back({ x1_l, y1, c1_l[0], c1_l[1], c1_l[2], c1_l[3] });
}

int main(int argc, char** argv)
{
	std::string resolved_model = litert_find_model(model_path_arg);

	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "llm_pingpong", reqs);

	// If the user specified an override on CLI, re-evaluate model path
	if (!model_path_arg.empty())
	{
		resolved_model = litert_find_model(model_path_arg);
	}

	litert_config_t lcfg;
	lcfg.model_path = resolved_model;
	lcfg.backend = backend_arg;
	lcfg.max_output_tokens = 48;
	lcfg.temperature = temperature_arg;
	lcfg.seed = seed_arg;
	lcfg.enable_benchmark = false;

	litert_context_t llm_ctx{};
	bool llm_ok = litert_init(llm_ctx, lcfg);
	if (!llm_ok && backend_arg != "cpu")
	{
		// Attempt CPU fallback if GPU init was not possible
		printf("Retrying LiteRT-LM initialization with CPU backend...\n");
		lcfg.backend = "cpu";
		llm_ok = litert_init(llm_ctx, lcfg);
	}

	if (!llm_ok)
	{
		fprintf(stderr, "Skipping test: Unable to initialize LiteRT-LM (unsupported or model not found).\n");
		test_done(vulkan);
		return 77;
	}

	// 1. Preprocessing: Ask LLM for paddle geometry and vibrant color palette
	printf("Querying LLM for paddle mesh and vertex palette...\n");
	std::string palette_prompt =
		"Provide 4 vibrant, high-contrast hex colors separated by comma for a ping-pong paddle vertices (e.g. #FF0055, #00FFAA, #FFFF00, #AA00FF):";
	std::string palette_resp = litert_query(llm_ctx, palette_prompt);
	printf("LLM Palette Response: %s\n", palette_resp.c_str());
	litert_reset_session(llm_ctx);

	const int NUM_SLICES = 6;
	const int NUM_POINTS = NUM_SLICES + 1;
	float slice_widths[NUM_POINTS];
	float paddle_colors[NUM_POINTS * 2][4];

	// Derive geometric width variations and striking per-vertex colors from LLM response
	unsigned int base_hash = string_hash(palette_resp);
	for (int i = 0; i < NUM_POINTS; ++i)
	{
		unsigned int h_i = base_hash ^ (i * 2654435761u);
		float var = ((h_i % 100) / 100.0f);
		slice_widths[i] = 0.015f + 0.040f * var;

		// High-contrast vibrant colors per vertex
		float hue_l = std::fmod((base_hash % 360) + i * 51.0f, 360.0f);
		float rgb_left[3];
		hsv_to_rgb(hue_l, 0.95f, 1.0f, rgb_left);
		paddle_colors[i * 2 + 0][0] = rgb_left[0];
		paddle_colors[i * 2 + 0][1] = rgb_left[1];
		paddle_colors[i * 2 + 0][2] = rgb_left[2];
		paddle_colors[i * 2 + 0][3] = 1.0f;

		float hue_r = std::fmod(hue_l + 180.0f, 360.0f); // Complementary hue on opposite side
		float rgb_right[3];
		hsv_to_rgb(hue_r, 0.95f, 1.0f, rgb_right);
		paddle_colors[i * 2 + 1][0] = rgb_right[0];
		paddle_colors[i * 2 + 1][1] = rgb_right[1];
		paddle_colors[i * 2 + 1][2] = rgb_right[2];
		paddle_colors[i * 2 + 1][3] = 1.0f;
	}

	// If the LLM successfully generated any hex colors, incorporate them
	std::string token;
	int color_idx = 0;
	for (size_t i = 0; i <= palette_resp.size() && color_idx < NUM_POINTS * 2; ++i)
	{
		char ch = (i < palette_resp.size()) ? palette_resp[i] : ',';
		if (ch == ',' || ch == '\n' || ch == ' ')
		{
			float r, g, b;
			if (parse_hex_color(token, r, g, b))
			{
				paddle_colors[color_idx][0] = r;
				paddle_colors[color_idx][1] = g;
				paddle_colors[color_idx][2] = b;
				paddle_colors[color_idx][3] = 1.0f;
				color_idx++;
			}
			token.clear();
		}
		else
		{
			token.push_back(ch);
		}
	}

	const uint32_t width = 640;
	const uint32_t height = 480;
	const VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;

	// Create offscreen render target image
	VkImage color_image = VK_NULL_HANDLE;
	VkDeviceMemory color_memory = VK_NULL_HANDLE;
	VkImageView color_view = VK_NULL_HANDLE;

	VkImageCreateInfo image_ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	image_ci.imageType = VK_IMAGE_TYPE_2D;
	image_ci.format = color_format;
	image_ci.extent = { width, height, 1 };
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = 1;
	image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
	image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult result = vkCreateImage(vulkan.device, &image_ci, nullptr, &color_image);
	check(result);

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(vulkan.device, color_image, &mem_reqs);
	VkMemoryAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &color_memory);
	check(result);
	result = vkBindImageMemory(vulkan.device, color_image, color_memory, 0);
	check(result);

	VkImageViewCreateInfo view_ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
	view_ci.image = color_image;
	view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_ci.format = color_format;
	view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_ci.subresourceRange.levelCount = 1;
	view_ci.subresourceRange.layerCount = 1;
	result = vkCreateImageView(vulkan.device, &view_ci, nullptr, &color_view);
	check(result);

	// RenderPass
	VkAttachmentDescription attachment{};
	attachment.format = color_format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	VkAttachmentReference color_ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;

	VkRenderPassCreateInfo rp_ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr };
	rp_ci.attachmentCount = 1;
	rp_ci.pAttachments = &attachment;
	rp_ci.subpassCount = 1;
	rp_ci.pSubpasses = &subpass;
	VkRenderPass render_pass = VK_NULL_HANDLE;
	result = vkCreateRenderPass(vulkan.device, &rp_ci, nullptr, &render_pass);
	check(result);

	// Framebuffer
	VkFramebufferCreateInfo fb_ci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr };
	fb_ci.renderPass = render_pass;
	fb_ci.attachmentCount = 1;
	fb_ci.pAttachments = &color_view;
	fb_ci.width = width;
	fb_ci.height = height;
	fb_ci.layers = 1;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	result = vkCreateFramebuffer(vulkan.device, &fb_ci, nullptr, &framebuffer);
	check(result);

	// Shaders & Pipeline
	VkShaderModuleCreateInfo vert_ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	vert_ci.codeSize = sizeof(vulkan_llm_pingpong_vert_spirv);
	vert_ci.pCode = reinterpret_cast<const uint32_t*>(vulkan_llm_pingpong_vert_spirv);
	VkShaderModule vert_module = VK_NULL_HANDLE;
	result = vkCreateShaderModule(vulkan.device, &vert_ci, nullptr, &vert_module);
	check(result);

	VkShaderModuleCreateInfo frag_ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	frag_ci.codeSize = sizeof(vulkan_llm_pingpong_frag_spirv);
	frag_ci.pCode = reinterpret_cast<const uint32_t*>(vulkan_llm_pingpong_frag_spirv);
	VkShaderModule frag_module = VK_NULL_HANDLE;
	result = vkCreateShaderModule(vulkan.device, &frag_ci, nullptr, &frag_module);
	check(result);

	VkPipelineLayoutCreateInfo layout_ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(vulkan.device, &layout_ci, nullptr, &pipeline_layout);
	check(result);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert_module;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag_module;
	stages[1].pName = "main";

	VkVertexInputBindingDescription binding_desc{};
	binding_desc.binding = 0;
	binding_desc.stride = sizeof(Vertex);
	binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrib_descs[2]{};
	attrib_descs[0].location = 0;
	attrib_descs[0].binding = 0;
	attrib_descs[0].format = VK_FORMAT_R32G32_SFLOAT;
	attrib_descs[0].offset = offsetof(Vertex, x);
	attrib_descs[1].location = 1;
	attrib_descs[1].binding = 0;
	attrib_descs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attrib_descs[1].offset = offsetof(Vertex, r);

	VkPipelineVertexInputStateCreateInfo vertex_input_ci{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr };
	vertex_input_ci.vertexBindingDescriptionCount = 1;
	vertex_input_ci.pVertexBindingDescriptions = &binding_desc;
	vertex_input_ci.vertexAttributeDescriptionCount = 2;
	vertex_input_ci.pVertexAttributeDescriptions = attrib_descs;

	VkPipelineInputAssemblyStateCreateInfo ia_ci{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr };
	ia_ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
	VkRect2D scissor{ { 0, 0 }, { width, height } };
	VkPipelineViewportStateCreateInfo vp_ci{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr };
	vp_ci.viewportCount = 1;
	vp_ci.pViewports = &viewport;
	vp_ci.scissorCount = 1;
	vp_ci.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo raster_ci{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr };
	raster_ci.polygonMode = VK_POLYGON_MODE_FILL;
	raster_ci.cullMode = VK_CULL_MODE_NONE;
	raster_ci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster_ci.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms_ci{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr };
	ms_ci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blend_att{};
	blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo blend_ci{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr };
	blend_ci.attachmentCount = 1;
	blend_ci.pAttachments = &blend_att;

	VkGraphicsPipelineCreateInfo pipe_ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };
	pipe_ci.stageCount = 2;
	pipe_ci.pStages = stages;
	pipe_ci.pVertexInputState = &vertex_input_ci;
	pipe_ci.pInputAssemblyState = &ia_ci;
	pipe_ci.pViewportState = &vp_ci;
	pipe_ci.pRasterizationState = &raster_ci;
	pipe_ci.pMultisampleState = &ms_ci;
	pipe_ci.pColorBlendState = &blend_ci;
	pipe_ci.layout = pipeline_layout;
	pipe_ci.renderPass = render_pass;
	pipe_ci.subpass = 0;

	VkPipeline pipeline = VK_NULL_HANDLE;
	result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipe_ci, nullptr, &pipeline);
	check(result);

	// Vertex buffer (dynamic, host visible)
	const VkDeviceSize vertex_buffer_size = 128 * sizeof(Vertex);
	VkBuffer vertex_buffer = VK_NULL_HANDLE;
	VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
	void* mapped_vertices = nullptr;

	VkBufferCreateInfo buf_ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buf_ci.size = vertex_buffer_size;
	buf_ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &buf_ci, nullptr, &vertex_buffer);
	check(result);

	VkMemoryRequirements buf_mem_reqs{};
	vkGetBufferMemoryRequirements(vulkan.device, vertex_buffer, &buf_mem_reqs);
	VkMemoryAllocateInfo buf_alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	buf_alloc.allocationSize = buf_mem_reqs.size;
	buf_alloc.memoryTypeIndex = get_device_memory_type(
		buf_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	result = vkAllocateMemory(vulkan.device, &buf_alloc, nullptr, &vertex_memory);
	check(result);
	result = vkBindBufferMemory(vulkan.device, vertex_buffer, vertex_memory, 0);
	check(result);
	result = vkMapMemory(vulkan.device, vertex_memory, 0, VK_WHOLE_SIZE, 0, &mapped_vertices);
	check(result);
	assert(mapped_vertices != nullptr);

	// Command Pool & Buffer
	VkCommandPoolCreateInfo cp_ci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	cp_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cp_ci.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &cp_ci, nullptr, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo cb_ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	cb_ai.commandPool = command_pool;
	cb_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cb_ai.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &cb_ai, &command_buffer);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);

	// Setup readback buffer if image saving is enabled
	VkBuffer readback_buffer = VK_NULL_HANDLE;
	VkDeviceMemory readback_memory = VK_NULL_HANDLE;
	const VkDeviceSize readback_size = width * height * 4;

	if (save_image_output || save_all_frames)
	{
		VkBufferCreateInfo r_buf_ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
		r_buf_ci.size = readback_size;
		r_buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		r_buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		result = vkCreateBuffer(vulkan.device, &r_buf_ci, nullptr, &readback_buffer);
		check(result);

		VkMemoryRequirements r_mem_reqs{};
		vkGetBufferMemoryRequirements(vulkan.device, readback_buffer, &r_mem_reqs);
		VkMemoryAllocateInfo r_alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
		r_alloc.allocationSize = r_mem_reqs.size;
		r_alloc.memoryTypeIndex = get_device_memory_type(
			r_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		result = vkAllocateMemory(vulkan.device, &r_alloc, nullptr, &readback_memory);
		check(result);
		result = vkBindBufferMemory(vulkan.device, readback_buffer, readback_memory, 0);
		check(result);
	}

	// Ping-pong game state
	float ball_x = 0.0f;
	float ball_y = 0.0f;
	float ball_vx = 0.04f;
	float ball_vy = 0.025f;
	const float ball_size = 0.04f;

	float paddle_y = 0.0f;
	float paddle_vy = 0.0f;
	const float paddle_x = -0.85f;
	const float paddle_half_w = 0.03f;
	const float paddle_half_h = 0.22f;
	const float paddle_speed = 0.04f;

	const float wall_top = 0.90f;
	const float wall_bottom = -0.90f;
	const float wall_right = 0.90f;
	const float wall_thickness = 0.04f;

	const float wall_color[4] = { 0.4f, 0.4f, 0.5f, 1.0f };
	float ball_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	bench_start_iteration(vulkan.bench);

	for (int frame = 0; frame < total_frames; ++frame)
	{
		// 2. Physics update
		ball_x += ball_vx;
		ball_y += ball_vy;

		// Bounce against top/bottom walls
		if (ball_y + ball_size >= wall_top)
		{
			ball_y = wall_top - ball_size;
			ball_vy = -std::abs(ball_vy);
		}
		else if (ball_y - ball_size <= wall_bottom)
		{
			ball_y = wall_bottom + ball_size;
			ball_vy = std::abs(ball_vy);
		}

		// EVENT: Bounce against right wall -> heading toward bot paddle!
		if (ball_x + ball_size >= wall_right)
		{
			ball_x = wall_right - ball_size;
			ball_vx = -std::abs(ball_vx);

			// Query the LLM to make a bot move decision
			char prompt_buf[256];
			snprintf(prompt_buf, sizeof(prompt_buf),
				"Ball rebounded! Ball y=%.2f, vy=%.2f. Bot paddle y=%.2f. Frame=%d. Reply 'UP', 'DOWN', or 'STAY':",
				ball_y, ball_vy, paddle_y, frame);

			std::string decision = litert_query(llm_ctx, prompt_buf);
			printf("[Frame %d] LLM Bot Decision: %s\n", frame, decision.c_str());

			unsigned int dec_hash = string_hash(decision);

			// Impart spin and vertical impulse directly from LLM reaction
			float angle_shift = (((dec_hash % 100) - 50) / 50.0f) * 0.035f;
			ball_vy += angle_shift;
			if (std::abs(ball_vy) < 0.015f) ball_vy = (ball_vy >= 0.0f ? 0.02f : -0.02f);
			if (std::abs(ball_vy) > 0.065f) ball_vy = (ball_vy >= 0.0f ? 0.065f : -0.065f);

			// Shift paddle vertex colors dynamically when LLM reacts
			for (int p = 0; p < NUM_POINTS * 2; ++p)
			{
				float h = std::fmod((dec_hash * 17u + p * 43u), 360.0f);
				float rgb[3];
				hsv_to_rgb(h, 0.95f, 1.0f, rgb);
				paddle_colors[p][0] = rgb[0];
				paddle_colors[p][1] = rgb[1];
				paddle_colors[p][2] = rgb[2];
			}

			// Flash ball color with striking LLM reaction hue
			float shift_hue = std::fmod(dec_hash % 360, 360.0f);
			float ball_rgb[3];
			hsv_to_rgb(shift_hue, 0.90f, 1.0f, ball_rgb);
			ball_color[0] = ball_rgb[0];
			ball_color[1] = ball_rgb[1];
			ball_color[2] = ball_rgb[2];

			if (decision.find("UP") != std::string::npos || decision.find("up") != std::string::npos)
			{
				paddle_vy = paddle_speed;
			}
			else if (decision.find("DOWN") != std::string::npos || decision.find("down") != std::string::npos)
			{
				paddle_vy = -paddle_speed;
			}
			else if (decision.find("STAY") != std::string::npos || decision.find("stay") != std::string::npos)
			{
				paddle_vy = 0.0f;
			}
			else
			{
				// Fallback heuristic parameterized by decision hash
				paddle_vy = ((dec_hash % 2) == 0) ? paddle_speed : -paddle_speed;
			}
		}

		// Update paddle position
		paddle_y += paddle_vy;
		if (paddle_y + paddle_half_h > wall_top)
		{
			paddle_y = wall_top - paddle_half_h;
			paddle_vy = 0.0f;
		}
		else if (paddle_y - paddle_half_h < wall_bottom)
		{
			paddle_y = wall_bottom + paddle_half_h;
			paddle_vy = 0.0f;
		}

		// Bounce against paddle
		if (ball_x - ball_size <= paddle_x + paddle_half_w &&
		    ball_x + ball_size >= paddle_x - paddle_half_w &&
		    ball_y >= paddle_y - paddle_half_h - ball_size &&
		    ball_y <= paddle_y + paddle_half_h + ball_size)
		{
			ball_x = paddle_x + paddle_half_w + ball_size;
			ball_vx = std::abs(ball_vx);
		}
		else if (ball_x < -1.0f)
		{
			// Reset ball to center if missed
			ball_x = 0.0f;
			ball_y = 0.0f;
			ball_vx = 0.04f;
		}

		// 3. Assemble dynamic vertices
		std::vector<Vertex> verts;
		verts.reserve(70);

		// Top wall
		add_quad(verts, -1.0f, wall_top, 1.0f, wall_top + wall_thickness,
		         wall_color, wall_color, wall_color, wall_color);
		// Bottom wall
		add_quad(verts, -1.0f, wall_bottom - wall_thickness, 1.0f, wall_bottom,
		         wall_color, wall_color, wall_color, wall_color);
		// Right wall
		add_quad(verts, wall_right, wall_bottom, wall_right + wall_thickness, wall_top,
		         wall_color, wall_color, wall_color, wall_color);

		// Ball
		add_quad(verts, ball_x - ball_size, ball_y - ball_size, ball_x + ball_size, ball_y + ball_size,
		         ball_color, ball_color, ball_color, ball_color);

		// AI Paddle as a multi-slice polygon mesh with LLM-generated vertex colors and shape
		for (int s = 0; s < NUM_SLICES; ++s)
		{
			float y0 = paddle_y - paddle_half_h + (2.0f * paddle_half_h * s) / NUM_SLICES;
			float y1 = paddle_y - paddle_half_h + (2.0f * paddle_half_h * (s + 1)) / NUM_SLICES;

			float w0 = slice_widths[s];
			float w1 = slice_widths[s + 1];

			const float* c0_l = paddle_colors[s * 2 + 0];
			const float* c0_r = paddle_colors[s * 2 + 1];
			const float* c1_l = paddle_colors[(s + 1) * 2 + 0];
			const float* c1_r = paddle_colors[(s + 1) * 2 + 1];

			add_trapezoid(verts,
			              paddle_x - w0, paddle_x + w0, y0,
			              paddle_x - w1, paddle_x + w1, y1,
			              c0_l, c0_r, c1_l, c1_r);
		}

		memcpy(mapped_vertices, verts.data(), verts.size() * sizeof(Vertex));

		if (vulkan.has_trace_helpers && vulkan.vkAssertBuffer)
		{
			uint32_t checksum = 0;
			const VkUpdateBufferInfoARM assert_info{
				VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, vertex_buffer, 0, VK_WHOLE_SIZE, nullptr
			};
			vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum, "pingpong vertex buffer");
		}

		// 4. Render frame
		VkCommandBufferBeginInfo begin_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(command_buffer, &begin_info);
		check(result);

		VkClearValue clear_color{ { { 0.05f, 0.05f, 0.08f, 1.0f } } };
		VkRenderPassBeginInfo rp_begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
		rp_begin.renderPass = render_pass;
		rp_begin.framebuffer = framebuffer;
		rp_begin.renderArea.offset = { 0, 0 };
		rp_begin.renderArea.extent = { width, height };
		rp_begin.clearValueCount = 1;
		rp_begin.pClearValues = &clear_color;

		vkCmdBeginRenderPass(command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &offset);
		vkCmdDraw(command_buffer, static_cast<uint32_t>(verts.size()), 1, 0, 0);

		vkCmdEndRenderPass(command_buffer);

		result = vkEndCommandBuffer(command_buffer);
		check(result);

		VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;
		result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
		check(result);

		result = vkQueueWaitIdle(queue);
		check(result);

		bool should_save = save_all_frames ||
		                   (target_save_frame >= 0 && frame == target_save_frame) ||
		                   (!save_all_frames && target_save_frame < 0 && save_image_output && frame == total_frames - 1);

		if (should_save && readback_buffer != VK_NULL_HANDLE)
		{
			VkCommandBufferBeginInfo cb_bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
			cb_bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			result = vkBeginCommandBuffer(command_buffer, &cb_bi);
			check(result);

			VkBufferImageCopy copy_region{};
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageExtent = { width, height, 1 };
			vkCmdCopyImageToBuffer(command_buffer, color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			                       readback_buffer, 1, &copy_region);

			result = vkEndCommandBuffer(command_buffer);
			check(result);

			VkSubmitInfo r_submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
			r_submit.commandBufferCount = 1;
			r_submit.pCommandBuffers = &command_buffer;
			result = vkQueueSubmit(queue, 1, &r_submit, VK_NULL_HANDLE);
			check(result);
			result = vkQueueWaitIdle(queue);
			check(result);

			char out_name[256];
			if (save_all_frames || target_save_frame >= 0)
			{
				snprintf(out_name, sizeof(out_name), "llm_pingpong_%04d.png", frame);
			}
			else
			{
				snprintf(out_name, sizeof(out_name), "%s", image_filename.c_str());
			}

			test_save_image(vulkan, out_name, readback_memory, 0, width, height, color_format);
			printf("Saved frame %d to %s\n", frame, out_name);
		}
	}

	bench_stop_iteration(vulkan.bench);

	if (readback_buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan.device, readback_buffer, nullptr);
		vkFreeMemory(vulkan.device, readback_memory, nullptr);
	}

	// Cleanup
	vkUnmapMemory(vulkan.device, vertex_memory);
	vkDestroyBuffer(vulkan.device, vertex_buffer, nullptr);
	vkFreeMemory(vulkan.device, vertex_memory, nullptr);

	vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyShaderModule(vulkan.device, vert_module, nullptr);
	vkDestroyShaderModule(vulkan.device, frag_module, nullptr);

	vkDestroyFramebuffer(vulkan.device, framebuffer, nullptr);
	vkDestroyRenderPass(vulkan.device, render_pass, nullptr);
	vkDestroyImageView(vulkan.device, color_view, nullptr);
	vkDestroyImage(vulkan.device, color_image, nullptr);
	vkFreeMemory(vulkan.device, color_memory, nullptr);

	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);

	litert_cleanup(llm_ctx);
	test_done(vulkan);

	printf("vulkan_llm_pingpong completed successfully!\n");
	return 0;
}
