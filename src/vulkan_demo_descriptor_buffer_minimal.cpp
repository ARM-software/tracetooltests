// Minimal graphics test using VK_EXT_descriptor_buffer.
// Based on the demo from Sascha Willem's vulkan examples.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our shaders, generated with:
//   xxd -i -n vulkan_demo_descriptor_buffer_minimal_vert_spv content/vulkan-demos/shaders/glsl/descriptorbuffer/cube.vert.spv > src/vulkan_demo_descriptor_buffer_minimal_vert.inc
//   xxd -i -n vulkan_demo_descriptor_buffer_minimal_frag_spv content/vulkan-demos/shaders/glsl/descriptorbuffer/cube.frag.spv > src/vulkan_demo_descriptor_buffer_minimal_frag.inc
#include "vulkan_demo_descriptor_buffer_minimal_vert.inc"
#include "vulkan_demo_descriptor_buffer_minimal_frag.inc"

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
	glm::vec2 uv;
	glm::vec3 color;
};

static const std::array<Vertex, 6> kQuad = {{
	{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.3f, 0.2f}},
	{{ 1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.2f, 1.0f, 0.3f}},
	{{ 1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.2f, 0.3f, 1.0f}},
	{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.3f, 0.2f}},
	{{ 1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.2f, 0.3f, 1.0f}},
	{{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.9f, 0.9f, 0.9f}},
}};

struct CameraUBO
{
	glm::mat4 projection;
	glm::mat4 view;
};

struct ModelUBO
{
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

struct DescriptorBufferFunctions
{
	PFN_vkGetDescriptorSetLayoutSizeEXT getLayoutSize = nullptr;
	PFN_vkGetDescriptorSetLayoutBindingOffsetEXT getLayoutBindingOffset = nullptr;
	PFN_vkGetDescriptorEXT getDescriptor = nullptr;
	PFN_vkCmdBindDescriptorBuffersEXT cmdBindDescriptorBuffers = nullptr;
	PFN_vkCmdSetDescriptorBufferOffsetsEXT cmdSetDescriptorBufferOffsets = nullptr;
};

struct DescriptorBufferBinding
{
	VkDeviceSize layoutSize = 0;
	VkDeviceSize layoutOffset = 0;
	VkBufferUsageFlags usage = 0;
	std::unique_ptr<Buffer> buffer;
	VkDeviceAddress address = 0;
};

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
	if (alignment == 0)
		return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static DescriptorBufferFunctions load_descriptor_buffer_functions(const vulkan_setup_t& vulkan)
{
	DescriptorBufferFunctions funcs{};
	funcs.getLayoutSize = reinterpret_cast<PFN_vkGetDescriptorSetLayoutSizeEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkGetDescriptorSetLayoutSizeEXT"));
	funcs.getLayoutBindingOffset = reinterpret_cast<PFN_vkGetDescriptorSetLayoutBindingOffsetEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkGetDescriptorSetLayoutBindingOffsetEXT"));
	funcs.getDescriptor = reinterpret_cast<PFN_vkGetDescriptorEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkGetDescriptorEXT"));
	funcs.cmdBindDescriptorBuffers = reinterpret_cast<PFN_vkCmdBindDescriptorBuffersEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkCmdBindDescriptorBuffersEXT"));
	funcs.cmdSetDescriptorBufferOffsets = reinterpret_cast<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkCmdSetDescriptorBufferOffsetsEXT"));
	assert(funcs.getLayoutSize);
	assert(funcs.getLayoutBindingOffset);
	assert(funcs.getDescriptor);
	assert(funcs.cmdBindDescriptorBuffers);
	assert(funcs.cmdSetDescriptorBufferOffsets);
	return funcs;
}

static VkPhysicalDeviceDescriptorBufferPropertiesEXT get_descriptor_buffer_properties(VkPhysicalDevice physical)
{
	VkPhysicalDeviceDescriptorBufferPropertiesEXT props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT, nullptr};
	VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &props};
	vkGetPhysicalDeviceProperties2(physical, &props2);
	return props;
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

class DescriptorBufferContext : public GraphicContext
{
public:
	DescriptorBufferContext() : GraphicContext() {}
	~DescriptorBufferContext() {
		destroy();
	}

	void destroy()
	{
		colorTarget = {};

		vertexBuffer = nullptr;
		cameraUbo = nullptr;
		modelUbo = nullptr;

		sampler = nullptr;
		texView = nullptr;
		texImage = nullptr;

		uniformDesc.buffer = nullptr;
		imageDesc.buffer = nullptr;

		pipeline = nullptr;
		pipelineLayout = nullptr;

		if (frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, frameFence, nullptr);
			frameFence = VK_NULL_HANDLE;
		}
	}

	ColorTarget colorTarget;

	std::unique_ptr<Buffer> vertexBuffer;
	std::unique_ptr<Buffer> cameraUbo;
	std::unique_ptr<Buffer> modelUbo;

	std::unique_ptr<Sampler> sampler;
	std::shared_ptr<Image> texImage;
	std::shared_ptr<ImageView> texView;

	DescriptorBufferBinding uniformDesc;
	DescriptorBufferBinding imageDesc;

	std::shared_ptr<PipelineLayout> pipelineLayout;
	std::unique_ptr<GraphicPipeline> pipeline;

	DescriptorBufferFunctions dbFuncs{};
	VkFence frameFence = VK_NULL_HANDLE;
};

static std::unique_ptr<DescriptorBufferContext> p_benchmark = nullptr;

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

		VkBuffer vertex_buffers[] = {p_benchmark->vertexBuffer->getHandle()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);

		VkDescriptorBufferBindingInfoEXT bindings[2]{};
		bindings[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
		bindings[0].address = p_benchmark->uniformDesc.address;
		bindings[0].usage = p_benchmark->uniformDesc.usage;
		bindings[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
		bindings[1].address = p_benchmark->imageDesc.address;
		bindings[1].usage = p_benchmark->imageDesc.usage;
		p_benchmark->dbFuncs.cmdBindDescriptorBuffers(cmd, 2, bindings);

		uint32_t buffer_index = 0;
		VkDeviceSize offset = 0;
		p_benchmark->dbFuncs.cmdSetDescriptorBufferOffsets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                                                   p_benchmark->pipelineLayout->getHandle(), 0, 1,
		                                                   &buffer_index, &offset);

		offset = p_benchmark->uniformDesc.layoutSize;
		p_benchmark->dbFuncs.cmdSetDescriptorBufferOffsets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                                                   p_benchmark->pipelineLayout->getHandle(), 1, 1,
		                                                   &buffer_index, &offset);

		uint32_t image_buffer_index = 1;
		offset = 0;
		p_benchmark->dbFuncs.cmdSetDescriptorBufferOffsets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                                                   p_benchmark->pipelineLayout->getHandle(), 2, 1,
		                                                   &image_buffer_index, &offset);

		vkCmdDraw(cmd, static_cast<uint32_t>(kQuad.size()), 1, 0, 0);

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
	p_benchmark = std::make_unique<DescriptorBufferContext>();

	VkPhysicalDeviceDescriptorBufferFeaturesEXT desc_buffer_feat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT, nullptr};
	desc_buffer_feat.descriptorBuffer = VK_TRUE;

	vulkan_req_t req{};
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.apiVersion = VK_API_VERSION_1_3;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.bufferDeviceAddress = true;
	req.device_extensions.push_back("VK_EXT_descriptor_buffer");
	req.extension_features = reinterpret_cast<VkBaseInStructure*>(&desc_buffer_feat);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_demo_descriptor_buffer_minimal", req);
	p_benchmark->initBasic(vulkan, req);
	p_benchmark->dbFuncs = load_descriptor_buffer_functions(vulkan);

	VkPhysicalDeviceDescriptorBufferPropertiesEXT desc_props = get_descriptor_buffer_properties(vulkan.physical);

	p_benchmark->colorTarget = create_color_target(vulkan, {p_benchmark->width, p_benchmark->height});

	auto vertexBuffer = std::make_unique<Buffer>(vulkan);
	vertexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                     sizeof(Vertex) * kQuad.size(), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	std::vector<Vertex> vertex_data(kQuad.begin(), kQuad.end());
	p_benchmark->updateBuffer(vertex_data, *vertexBuffer);

	auto cameraUbo = std::make_unique<Buffer>(vulkan);
	cameraUbo->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                  sizeof(CameraUBO), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	cameraUbo->map();
	CameraUBO camera_data{};
	camera_data.projection = glm::mat4(1.0f);
	camera_data.view = glm::mat4(1.0f);
	memcpy(cameraUbo->m_mappedAddress, &camera_data, sizeof(camera_data));
	cameraUbo->flush(true);
	cameraUbo->unmap();

	auto modelUbo = std::make_unique<Buffer>(vulkan);
	modelUbo->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                 sizeof(ModelUBO), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	modelUbo->map();
	ModelUBO model_data{};
	model_data.model = glm::mat4(1.0f);
	memcpy(modelUbo->m_mappedAddress, &model_data, sizeof(model_data));
	modelUbo->flush(true);
	modelUbo->unmap();

	const uint32_t tex_extent = 4;
	std::vector<uint8_t> tex_data(tex_extent * tex_extent * 4, 255);
	auto texImage = std::make_shared<Image>(vulkan.device);
	texImage->create({tex_extent, tex_extent, 1}, VK_FORMAT_R8G8B8A8_UNORM,
	                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto texView = std::make_shared<ImageView>(texImage);
	texView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	auto sampler = std::make_unique<Sampler>(vulkan.device);
	sampler->create(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
	                VK_FALSE, 1.0f);

	p_benchmark->updateImage(reinterpret_cast<const char*>(tex_data.data()), tex_data.size(), *texImage,
	                         {tex_extent, tex_extent, 1});
	p_benchmark->submitStaging(true, {}, {}, false);

	auto uniformLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	uniformLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	uniformLayout->create(static_cast<uint32_t>(uniformLayout->getBindings().size()),
	                      VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

	auto samplerLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	samplerLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	samplerLayout->create(static_cast<uint32_t>(samplerLayout->getBindings().size()),
	                      VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

	p_benchmark->uniformDesc.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	p_benchmark->dbFuncs.getLayoutSize(vulkan.device, uniformLayout->getHandle(), &p_benchmark->uniformDesc.layoutSize);
	p_benchmark->dbFuncs.getLayoutBindingOffset(vulkan.device, uniformLayout->getHandle(), 0, &p_benchmark->uniformDesc.layoutOffset);
	p_benchmark->uniformDesc.layoutSize = align_up(p_benchmark->uniformDesc.layoutSize, desc_props.descriptorBufferOffsetAlignment);
	p_benchmark->uniformDesc.buffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->uniformDesc.buffer->create(p_benchmark->uniformDesc.usage,
	                                        p_benchmark->uniformDesc.layoutSize * 2,
	                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	p_benchmark->uniformDesc.buffer->map();

	p_benchmark->imageDesc.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
	                               VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
	                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	p_benchmark->dbFuncs.getLayoutSize(vulkan.device, samplerLayout->getHandle(), &p_benchmark->imageDesc.layoutSize);
	p_benchmark->dbFuncs.getLayoutBindingOffset(vulkan.device, samplerLayout->getHandle(), 0, &p_benchmark->imageDesc.layoutOffset);
	p_benchmark->imageDesc.layoutSize = align_up(p_benchmark->imageDesc.layoutSize, desc_props.descriptorBufferOffsetAlignment);
	p_benchmark->imageDesc.buffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->imageDesc.buffer->create(p_benchmark->imageDesc.usage,
	                                      p_benchmark->imageDesc.layoutSize,
	                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	p_benchmark->imageDesc.buffer->map();

	VkDescriptorGetInfoEXT get_info{VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT, nullptr};
	VkDescriptorAddressInfoEXT addr_info{VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT, nullptr};
	addr_info.address = cameraUbo->getBufferDeviceAddress();
	addr_info.range = cameraUbo->getSize();
	addr_info.format = VK_FORMAT_UNDEFINED;
	get_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	get_info.data.pUniformBuffer = &addr_info;
	char* uniform_ptr = static_cast<char*>(p_benchmark->uniformDesc.buffer->m_mappedAddress);
	p_benchmark->dbFuncs.getDescriptor(vulkan.device, &get_info,
	                                   desc_props.uniformBufferDescriptorSize,
	                                   uniform_ptr + p_benchmark->uniformDesc.layoutOffset);

	addr_info.address = modelUbo->getBufferDeviceAddress();
	addr_info.range = modelUbo->getSize();
	get_info.data.pUniformBuffer = &addr_info;
	p_benchmark->dbFuncs.getDescriptor(vulkan.device, &get_info,
	                                   desc_props.uniformBufferDescriptorSize,
	                                   uniform_ptr + p_benchmark->uniformDesc.layoutSize + p_benchmark->uniformDesc.layoutOffset);

	VkDescriptorImageInfo image_info{};
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image_info.imageView = texView->getHandle();
	image_info.sampler = sampler->getHandle();
	get_info.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	get_info.data.pCombinedImageSampler = &image_info;
	char* image_ptr = static_cast<char*>(p_benchmark->imageDesc.buffer->m_mappedAddress);
	p_benchmark->dbFuncs.getDescriptor(vulkan.device, &get_info,
	                                   desc_props.combinedImageSamplerDescriptorSize,
	                                   image_ptr + p_benchmark->imageDesc.layoutOffset);

	p_benchmark->uniformDesc.buffer->flush(true);
	p_benchmark->imageDesc.buffer->flush(true);
	if (vulkan.has_explicit_host_updates)
	{
		testFlushMemory(vulkan, p_benchmark->uniformDesc.buffer->getMemory(), 0, p_benchmark->uniformDesc.buffer->getSize(), true);
		testFlushMemory(vulkan, p_benchmark->imageDesc.buffer->getMemory(), 0, p_benchmark->imageDesc.buffer->getSize(), true);
	}
	p_benchmark->uniformDesc.buffer->unmap();
	p_benchmark->imageDesc.buffer->unmap();

	p_benchmark->uniformDesc.address = p_benchmark->uniformDesc.buffer->getBufferDeviceAddress();
	p_benchmark->imageDesc.address = p_benchmark->imageDesc.buffer->getBufferDeviceAddress();

	{
		std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> layout_map = {
			{0, uniformLayout},
			{1, uniformLayout},
			{2, samplerLayout},
		};
		p_benchmark->pipelineLayout = std::make_shared<PipelineLayout>(vulkan.device);
		p_benchmark->pipelineLayout->create(layout_map);
	}

	{
		auto vertShader = std::make_shared<Shader>(vulkan.device);
		vertShader->create(vulkan_descriptor_buffer_minimal_vert_spv, vulkan_descriptor_buffer_minimal_vert_spv_len);
		auto fragShader = std::make_shared<Shader>(vulkan.device);
		fragShader->create(vulkan_descriptor_buffer_minimal_frag_spv, vulkan_descriptor_buffer_minimal_frag_spv_len);

		GraphicPipelineState pipelineState;
		pipelineState.setVertexBinding(0, *vertexBuffer, sizeof(Vertex));
		pipelineState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos));
		pipelineState.setVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal));
		pipelineState.setVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv));
		pipelineState.setVertexAttribute(3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));
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
		p_benchmark->pipeline->create({vertStage, fragStage}, pipelineState, *p_benchmark->colorTarget.renderPass,
		                              VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
	}

	p_benchmark->vertexBuffer = std::move(vertexBuffer);
	p_benchmark->cameraUbo = std::move(cameraUbo);
	p_benchmark->modelUbo = std::move(modelUbo);
	p_benchmark->sampler = std::move(sampler);
	p_benchmark->texImage = std::move(texImage);
	p_benchmark->texView = std::move(texView);

	uniformLayout = nullptr;
	samplerLayout = nullptr;

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
