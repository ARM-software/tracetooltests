// Minimal graphics test using VK_EXT_descriptor_heap.
// Based on vulkan_demo_descriptor_buffer_minimal.cpp.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our shaders, generated with:
//   xxd -i -n vulkan_demo_descriptor_buffer_minimal_vert_spv content/vulkan-demos/shaders/glsl/descriptorbuffer/cube.vert.spv > src/vulkan_demo_descriptor_buffer_minimal_vert.inc
//   xxd -i -n vulkan_demo_descriptor_buffer_minimal_frag_spv content/vulkan-demos/shaders/glsl/descriptorbuffer/cube.frag.spv > src/vulkan_demo_descriptor_buffer_minimal_frag.inc
#include "vulkan_demo_descriptor_buffer_minimal_vert.inc"
#include "vulkan_demo_descriptor_buffer_minimal_frag.inc"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
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

struct DescriptorHeapFunctions
{
	PFN_vkWriteSamplerDescriptorsEXT writeSamplerDescriptors = nullptr;
	PFN_vkWriteResourceDescriptorsEXT writeResourceDescriptors = nullptr;
	PFN_vkCmdBindSamplerHeapEXT cmdBindSamplerHeap = nullptr;
	PFN_vkCmdBindResourceHeapEXT cmdBindResourceHeap = nullptr;
};

struct DescriptorHeapBinding
{
	std::unique_ptr<Buffer> buffer;
	VkDeviceAddress base = 0;
	VkDeviceSize heapSize = 0;
	VkDeviceSize mappedOffset = 0;
	VkDeviceSize reservedOffset = 0;
	VkDeviceSize reservedSize = 0;
};

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
	if (alignment == 0)
		return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static VkDeviceAddress align_up_address(VkDeviceAddress value, VkDeviceSize alignment)
{
	if (alignment == 0)
		return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static DescriptorHeapFunctions load_descriptor_heap_functions(const vulkan_setup_t& vulkan)
{
	DescriptorHeapFunctions funcs{};
	funcs.writeSamplerDescriptors = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkWriteSamplerDescriptorsEXT"));
	funcs.writeResourceDescriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkWriteResourceDescriptorsEXT"));
	funcs.cmdBindSamplerHeap = reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkCmdBindSamplerHeapEXT"));
	funcs.cmdBindResourceHeap = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkCmdBindResourceHeapEXT"));
	assert(funcs.writeSamplerDescriptors);
	assert(funcs.writeResourceDescriptors);
	assert(funcs.cmdBindSamplerHeap);
	assert(funcs.cmdBindResourceHeap);
	return funcs;
}

static VkPhysicalDeviceDescriptorHeapPropertiesEXT get_descriptor_heap_properties(VkPhysicalDevice physical)
{
	VkPhysicalDeviceDescriptorHeapPropertiesEXT props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT, nullptr};
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

	AttachmentInfo color{0, *target.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	color.m_description.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.m_description.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	SubpassInfo subpass{};
	subpass.addColorAttachment(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	target.renderPass = std::make_shared<RenderPass>(vulkan.device);
	target.renderPass->create({color}, {subpass});

	target.framebuffer = std::make_shared<FrameBuffer>(vulkan.device);
	target.framebuffer->create(*target.renderPass, {target.view}, extent);

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

static VkImageViewCreateInfo make_texture_view_info(VkImage image, VkFormat format)
{
	VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	info.image = image;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.format = format;
	info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = 1;
	return info;
}

static VkSamplerCreateInfo make_sampler_info()
{
	VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr};
	info.magFilter = VK_FILTER_LINEAR;
	info.minFilter = VK_FILTER_LINEAR;
	info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.minLod = 0.0f;
	info.maxLod = 1.0f;
	info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	return info;
}

static DescriptorHeapBinding create_heap_binding(const vulkan_setup_t& vulkan, VkDeviceSize heap_size, VkDeviceSize heap_alignment,
                                                 VkDeviceSize reserved_offset, VkDeviceSize reserved_size, const char* name)
{
	DescriptorHeapBinding binding{};
	binding.heapSize = heap_size;
	binding.reservedOffset = reserved_offset;
	binding.reservedSize = reserved_size;

	const VkDeviceSize allocation_size = heap_size + std::max(heap_alignment, VkDeviceSize(1)) - 1;
	binding.buffer = std::make_unique<Buffer>(vulkan);
	binding.buffer->create(VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                       allocation_size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	binding.buffer->map();
	std::memset(binding.buffer->m_mappedAddress, 0, static_cast<size_t>(binding.buffer->getSize()));

	const VkDeviceAddress device_address = binding.buffer->getBufferDeviceAddress();
	assert(device_address != 0);
	binding.base = align_up_address(device_address, heap_alignment);
	binding.mappedOffset = binding.base - device_address;
	assert(binding.mappedOffset + heap_size <= binding.buffer->getSize());

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)binding.buffer->getHandle(), name);
	return binding;
}

static VkDescriptorSetAndBindingMappingEXT make_constant_mapping(uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT mask,
                                                                 VkDeviceSize heap_offset, VkDeviceSize heap_stride,
                                                                 VkDeviceSize sampler_heap_offset = 0,
                                                                 VkDeviceSize sampler_heap_stride = 0)
{
	assert(heap_offset <= std::numeric_limits<uint32_t>::max());
	assert(heap_stride <= std::numeric_limits<uint32_t>::max());
	assert(sampler_heap_offset <= std::numeric_limits<uint32_t>::max());
	assert(sampler_heap_stride <= std::numeric_limits<uint32_t>::max());

	VkDescriptorSetAndBindingMappingEXT mapping{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT, nullptr};
	mapping.descriptorSet = set;
	mapping.firstBinding = binding;
	mapping.bindingCount = 1;
	mapping.resourceMask = mask;
	mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
	mapping.sourceData.constantOffset.heapOffset = static_cast<uint32_t>(heap_offset);
	mapping.sourceData.constantOffset.heapArrayStride = static_cast<uint32_t>(heap_stride);
	mapping.sourceData.constantOffset.pEmbeddedSampler = nullptr;
	mapping.sourceData.constantOffset.samplerHeapOffset = static_cast<uint32_t>(sampler_heap_offset);
	mapping.sourceData.constantOffset.samplerHeapArrayStride = static_cast<uint32_t>(sampler_heap_stride);
	return mapping;
}

class DescriptorHeapContext : public GraphicContext
{
public:
	DescriptorHeapContext() : GraphicContext() {}
	~DescriptorHeapContext() {
		destroy();
	}

	void destroy()
	{
		if (pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_vulkanSetup.device, pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}

		colorTarget = {};

		vertexBuffer = nullptr;
		cameraUbo = nullptr;
		modelUbo = nullptr;
		texImage = nullptr;

		resourceHeap.buffer = nullptr;
		samplerHeap.buffer = nullptr;

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
	std::shared_ptr<Image> texImage;

	DescriptorHeapBinding resourceHeap;
	DescriptorHeapBinding samplerHeap;

	VkPipeline pipeline = VK_NULL_HANDLE;

	DescriptorHeapFunctions heapFuncs{};
	VkFence frameFence = VK_NULL_HANDLE;
};

static std::unique_ptr<DescriptorHeapContext> p_benchmark = nullptr;

static VkPipeline create_pipeline(const vulkan_setup_t& vulkan, VkRenderPass render_pass,
                                  const std::array<VkDescriptorSetAndBindingMappingEXT, 2>& vertex_mappings,
                                  const std::array<VkDescriptorSetAndBindingMappingEXT, 1>& fragment_mappings)
{
	VkShaderModuleCreateInfo vert_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	vert_info.codeSize = vulkan_descriptor_buffer_minimal_vert_spv_len;
	vert_info.pCode = reinterpret_cast<const uint32_t*>(vulkan_descriptor_buffer_minimal_vert_spv);
	VkShaderModule vert_module = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &vert_info, nullptr, &vert_module);
	check(result);

	VkShaderModuleCreateInfo frag_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	frag_info.codeSize = vulkan_descriptor_buffer_minimal_frag_spv_len;
	frag_info.pCode = reinterpret_cast<const uint32_t*>(vulkan_descriptor_buffer_minimal_frag_spv);
	VkShaderModule frag_module = VK_NULL_HANDLE;
	result = vkCreateShaderModule(vulkan.device, &frag_info, nullptr, &frag_module);
	check(result);

	VkShaderDescriptorSetAndBindingMappingInfoEXT vertex_mapping_info{VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT, nullptr};
	vertex_mapping_info.mappingCount = static_cast<uint32_t>(vertex_mappings.size());
	vertex_mapping_info.pMappings = vertex_mappings.data();

	VkShaderDescriptorSetAndBindingMappingInfoEXT fragment_mapping_info{VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT, nullptr};
	fragment_mapping_info.mappingCount = static_cast<uint32_t>(fragment_mappings.size());
	fragment_mapping_info.pMappings = fragment_mappings.data();

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].pNext = &vertex_mapping_info;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert_module;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].pNext = &fragment_mapping_info;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag_module;
	stages[1].pName = "main";

	VkVertexInputBindingDescription vertex_binding{};
	vertex_binding.binding = 0;
	vertex_binding.stride = sizeof(Vertex);
	vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	std::array<VkVertexInputAttributeDescription, 4> vertex_attributes{};
	vertex_attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, pos))};
	vertex_attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, normal))};
	vertex_attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, uv))};
	vertex_attributes[3] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, color))};

	VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr};
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &vertex_binding;
	vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attributes.size());
	vertex_input.pVertexAttributeDescriptions = vertex_attributes.data();

	VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr};
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr};
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depth_stencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr};
	depth_stencil.depthTestEnable = VK_FALSE;
	depth_stencil.depthWriteEnable = VK_FALSE;
	depth_stencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

	VkPipelineColorBlendAttachmentState color_blend_attachment{};
	color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr};
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &color_blend_attachment;

	std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic_state{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr};
	dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
	dynamic_state.pDynamicStates = dynamic_states.data();

	VkPipelineCreateFlags2CreateInfo flags2{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO, nullptr};
	flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

	VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &flags2};
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = stages;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterization;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pDepthStencilState = &depth_stencil;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = VK_NULL_HANDLE;
	pipeline_info.renderPass = render_pass;
	pipeline_info.subpass = 0;

	VkPipeline pipeline = VK_NULL_HANDLE;
	result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(result);

	vkDestroyShaderModule(vulkan.device, frag_module, nullptr);
	vkDestroyShaderModule(vulkan.device, vert_module, nullptr);
	return pipeline;
}

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

		VkBuffer vertex_buffers[] = {p_benchmark->vertexBuffer->getHandle()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);

		VkBindHeapInfoEXT sampler_bind_info{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT, nullptr};
		sampler_bind_info.heapRange.address = p_benchmark->samplerHeap.base;
		sampler_bind_info.heapRange.size = p_benchmark->samplerHeap.heapSize;
		sampler_bind_info.reservedRangeOffset = p_benchmark->samplerHeap.reservedOffset;
		sampler_bind_info.reservedRangeSize = p_benchmark->samplerHeap.reservedSize;
		p_benchmark->heapFuncs.cmdBindSamplerHeap(cmd, &sampler_bind_info);

		VkBindHeapInfoEXT resource_bind_info{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT, nullptr};
		resource_bind_info.heapRange.address = p_benchmark->resourceHeap.base;
		resource_bind_info.heapRange.size = p_benchmark->resourceHeap.heapSize;
		resource_bind_info.reservedRangeOffset = p_benchmark->resourceHeap.reservedOffset;
		resource_bind_info.reservedRangeSize = p_benchmark->resourceHeap.reservedSize;
		p_benchmark->heapFuncs.cmdBindResourceHeap(cmd, &resource_bind_info);

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
	p_benchmark = std::make_unique<DescriptorHeapContext>();

	VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, nullptr};
	maintenance5_features.maintenance5 = VK_TRUE;
	VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT, &maintenance5_features};
	descriptor_heap_features.descriptorHeap = VK_TRUE;

	vulkan_req_t req{};
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.apiVersion = VK_API_VERSION_1_3;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.bufferDeviceAddress = true;
	req.device_extensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
	req.device_extensions.push_back(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
	req.extension_features = reinterpret_cast<VkBaseInStructure*>(&descriptor_heap_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_demo_descriptor_heap_minimal", req);
	p_benchmark->initBasic(vulkan, req);
	p_benchmark->heapFuncs = load_descriptor_heap_functions(vulkan);
	VkPhysicalDeviceDescriptorHeapPropertiesEXT desc_props = get_descriptor_heap_properties(vulkan.physical);
	assert(desc_props.bufferDescriptorSize > 0);
	assert(desc_props.imageDescriptorSize > 0);
	assert(desc_props.samplerDescriptorSize > 0);

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

	p_benchmark->updateImage(reinterpret_cast<const char*>(tex_data.data()), tex_data.size(), *texImage,
	                         {tex_extent, tex_extent, 1});
	p_benchmark->submitStaging(true, {}, {}, false);

	VkPhysicalDeviceProperties device_props{};
	vkGetPhysicalDeviceProperties(vulkan.physical, &device_props);
	assert((cameraUbo->getBufferDeviceAddress() % device_props.limits.minUniformBufferOffsetAlignment) == 0);
	assert((modelUbo->getBufferDeviceAddress() % device_props.limits.minUniformBufferOffsetAlignment) == 0);

	const VkDeviceSize camera_descriptor_offset = 0;
	const VkDeviceSize model_descriptor_offset = align_up(camera_descriptor_offset + desc_props.bufferDescriptorSize,
	                                                      desc_props.bufferDescriptorAlignment);
	const VkDeviceSize image_descriptor_offset = align_up(model_descriptor_offset + desc_props.bufferDescriptorSize,
	                                                      desc_props.imageDescriptorAlignment);
	const VkDeviceSize resource_app_end = image_descriptor_offset + desc_props.imageDescriptorSize;
	const VkDeviceSize resource_reserved_offset = align_up(resource_app_end, desc_props.resourceHeapAlignment);
	const VkDeviceSize resource_heap_size = resource_reserved_offset + desc_props.minResourceHeapReservedRange;
	assert(resource_heap_size <= desc_props.maxResourceHeapSize);

	const VkDeviceSize sampler_descriptor_offset = 0;
	const VkDeviceSize sampler_reserved_offset = align_up(desc_props.samplerDescriptorSize, desc_props.samplerHeapAlignment);
	const VkDeviceSize sampler_heap_size = sampler_reserved_offset + desc_props.minSamplerHeapReservedRange;
	assert(sampler_heap_size <= desc_props.maxSamplerHeapSize);

	p_benchmark->resourceHeap = create_heap_binding(vulkan, resource_heap_size, desc_props.resourceHeapAlignment,
	                                                resource_reserved_offset, desc_props.minResourceHeapReservedRange,
	                                                "demo_descriptor_heap_resource_heap");
	p_benchmark->samplerHeap = create_heap_binding(vulkan, sampler_heap_size, desc_props.samplerHeapAlignment,
	                                               sampler_reserved_offset, desc_props.minSamplerHeapReservedRange,
	                                               "demo_descriptor_heap_sampler_heap");

	VkDeviceAddressRangeEXT camera_range{};
	camera_range.address = cameraUbo->getBufferDeviceAddress();
	camera_range.size = cameraUbo->getSize();
	VkDeviceAddressRangeEXT model_range{};
	model_range.address = modelUbo->getBufferDeviceAddress();
	model_range.size = modelUbo->getSize();
	VkImageViewCreateInfo texture_view_info = make_texture_view_info(texImage->getHandle(), VK_FORMAT_R8G8B8A8_UNORM);
	VkImageDescriptorInfoEXT image_info{VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT, nullptr};
	image_info.pView = &texture_view_info;
	image_info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	std::array<VkResourceDescriptorInfoEXT, 3> resources{};
	resources[0].sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
	resources[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	resources[0].data.pAddressRange = &camera_range;
	resources[1].sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
	resources[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	resources[1].data.pAddressRange = &model_range;
	resources[2].sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
	resources[2].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	resources[2].data.pImage = &image_info;

	char* resource_heap_ptr = static_cast<char*>(p_benchmark->resourceHeap.buffer->m_mappedAddress) + p_benchmark->resourceHeap.mappedOffset;
	std::array<VkHostAddressRangeEXT, 3> resource_ranges{};
	resource_ranges[0] = {resource_heap_ptr + camera_descriptor_offset, static_cast<size_t>(desc_props.bufferDescriptorSize)};
	resource_ranges[1] = {resource_heap_ptr + model_descriptor_offset, static_cast<size_t>(desc_props.bufferDescriptorSize)};
	resource_ranges[2] = {resource_heap_ptr + image_descriptor_offset, static_cast<size_t>(desc_props.imageDescriptorSize)};
	VkResult result = p_benchmark->heapFuncs.writeResourceDescriptors(vulkan.device, static_cast<uint32_t>(resources.size()),
	                                                                  resources.data(), resource_ranges.data());
	check(result);

	VkSamplerCreateInfo sampler_info = make_sampler_info();
	char* sampler_heap_ptr = static_cast<char*>(p_benchmark->samplerHeap.buffer->m_mappedAddress) + p_benchmark->samplerHeap.mappedOffset;
	VkHostAddressRangeEXT sampler_range{};
	sampler_range.address = sampler_heap_ptr + sampler_descriptor_offset;
	sampler_range.size = static_cast<size_t>(desc_props.samplerDescriptorSize);
	result = p_benchmark->heapFuncs.writeSamplerDescriptors(vulkan.device, 1, &sampler_info, &sampler_range);
	check(result);

	testFlushMemoryDescriptors(vulkan, p_benchmark->resourceHeap.buffer->getMemory(), 0, p_benchmark->resourceHeap.buffer->getSize(),
	                           {p_benchmark->resourceHeap.mappedOffset + camera_descriptor_offset,
	                            p_benchmark->resourceHeap.mappedOffset + model_descriptor_offset,
	                            p_benchmark->resourceHeap.mappedOffset + image_descriptor_offset},
	                           {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE});
	testFlushMemoryDescriptors(vulkan, p_benchmark->samplerHeap.buffer->getMemory(), 0, p_benchmark->samplerHeap.buffer->getSize(),
	                           {p_benchmark->samplerHeap.mappedOffset + sampler_descriptor_offset},
	                           {VK_DESCRIPTOR_TYPE_SAMPLER});
	p_benchmark->resourceHeap.buffer->unmap();
	p_benchmark->samplerHeap.buffer->unmap();

	std::array<VkDescriptorSetAndBindingMappingEXT, 2> vertex_mappings = {
		make_constant_mapping(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
		                      camera_descriptor_offset, 0),
		make_constant_mapping(1, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
		                      model_descriptor_offset, 0),
	};
	std::array<VkDescriptorSetAndBindingMappingEXT, 1> fragment_mappings = {
		make_constant_mapping(2, 0, VK_SPIRV_RESOURCE_TYPE_COMBINED_SAMPLED_IMAGE_BIT_EXT,
		                      image_descriptor_offset, 0, sampler_descriptor_offset, 0),
	};

	p_benchmark->pipeline = create_pipeline(vulkan, p_benchmark->colorTarget.renderPass->getHandle(),
	                                        vertex_mappings, fragment_mappings);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)p_benchmark->pipeline, "demo_descriptor_heap_pipeline");
	test_marker_mention(vulkan, "Demo descriptor heaps are ready", VK_OBJECT_TYPE_BUFFER,
	                    (uint64_t)p_benchmark->resourceHeap.buffer->getHandle());

	p_benchmark->vertexBuffer = std::move(vertexBuffer);
	p_benchmark->cameraUbo = std::move(cameraUbo);
	p_benchmark->modelUbo = std::move(modelUbo);
	p_benchmark->texImage = std::move(texImage);

	VkFenceCreateInfo fence_info{};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &p_benchmark->frameFence);
	check(result);

	render(vulkan);

	result = vkDeviceWaitIdle(vulkan.device);
	check(result);

	p_benchmark = nullptr;
	return 0;
}
