#include "vulkan_common.h"
#include "vulkan_raytracing_common.h"

#include "vulkan_anki_scene2.vert.inc"
#include "vulkan_anki_scene2.frag.inc"

#include <array>
#include <cstring>
#include <string>

struct QuadVertex
{
	float pos[2];
	float uv[2];
};

struct alignas(16) CameraParams
{
	float origin_scale[4];
};

struct alignas(16) MaterialData
{
	float tint[4];
	float params[4];
};

struct alignas(16) HighlightData
{
	float accent[4];
};

struct ScenePointers
{
	VkDeviceAddress material_address;
};

struct alignas(16) FrameConfig
{
	float params[4];
};

struct alignas(16) PushConstants
{
	VkDeviceAddress highlight_address;
	float blend_factor;
	float padding[3];
};

static_assert(offsetof(PushConstants, highlight_address) == 0, "Trace helper marking expects the BDA at offset zero");

struct TextureResources
{
	VkImage image{VK_NULL_HANDLE};
	VkDeviceMemory memory{VK_NULL_HANDLE};
	VkImageView view{VK_NULL_HANDLE};
	VkSampler sampler{VK_NULL_HANDLE};
	VkFormat format{VK_FORMAT_R8G8B8A8_SRGB};
};

struct Resources
{
	ray_tracing_common::Context context;
	ray_tracing_common::SimpleAS accel;

	VkImage color_image{VK_NULL_HANDLE};
	VkDeviceMemory color_memory{VK_NULL_HANDLE};
	VkImageView color_view{VK_NULL_HANDLE};
	VkFormat color_format{VK_FORMAT_R8G8B8A8_UNORM};

	TextureResources albedo;

	acceleration_structures::Buffer vertex_buffer;
	acceleration_structures::Buffer index_buffer;
	acceleration_structures::Buffer camera_buffer;
	acceleration_structures::Buffer material_buffer;
	acceleration_structures::Buffer highlight_buffer;
	acceleration_structures::Buffer scene_pointers_buffer;
	acceleration_structures::Buffer frame_config_buffer;
	acceleration_structures::Buffer readback_buffer;

	VkRenderPass render_pass{VK_NULL_HANDLE};
	VkFramebuffer framebuffer{VK_NULL_HANDLE};

	VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
	VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};
	VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
	VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
	VkPipeline pipeline{VK_NULL_HANDLE};

	VkPipelineShaderStageCreateInfo shader_stages[2]{};
};

static constexpr uint32_t kRenderWidth = 128;
static constexpr uint32_t kRenderHeight = 128;
static constexpr uint32_t kTextureSize = 128;

static const std::array<QuadVertex, 4> kQuadVertices = {{
	{{-1.0f, -1.0f}, {0.0f, 0.0f}},
	{{ 1.0f, -1.0f}, {1.0f, 0.0f}},
	{{ 1.0f,  1.0f}, {1.0f, 1.0f}},
	{{-1.0f,  1.0f}, {0.0f, 1.0f}},
}};

static const std::array<uint16_t, 6> kQuadIndices = {{0, 1, 2, 2, 3, 0}};

static void show_usage()
{
	printf("Mocked Anki scene2 ray query test with explicit host updates and trace helper markup\n");
	printf("-i/--image-output      Save the rendered output to anki_scene2.png\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)i;
	(void)argc;
	if (match(argv[i], "-i", "--image-output"))
	{
		reqs.options["image_output"] = true;
		return true;
	}
	return false;
}

template <typename T>
static void upload_host_visible_struct(const vulkan_setup_t& vulkan, const acceleration_structures::Buffer& buffer, const T& value,
				       bool informative, VkMarkedOffsetsARM* markings = nullptr)
{
	void* mapped = nullptr;
	check(vkMapMemory(vulkan.device, buffer.memory, 0, sizeof(T), 0, &mapped));
	memcpy(mapped, &value, sizeof(T));
	if (informative || markings)
	{
		testFlushMemory(vulkan, buffer.memory, 0, sizeof(T), informative, markings);
	}
	vkUnmapMemory(vulkan.device, buffer.memory);
}

static void destroy_buffer(const vulkan_setup_t& vulkan, acceleration_structures::Buffer& buffer)
{
	if (buffer.memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(vulkan.device, buffer.memory, nullptr);
		buffer.memory = VK_NULL_HANDLE;
	}
	if (buffer.handle != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan.device, buffer.handle, nullptr);
		buffer.handle = VK_NULL_HANDLE;
	}
	buffer.address.deviceAddress = 0;
}

static void create_color_target(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = resources.color_format;
	image_info.extent = {kRenderWidth, kRenderHeight, 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	check(vkCreateImage(vulkan.device, &image_info, nullptr, &resources.color_image));
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)resources.color_image, "anki_scene2_color");

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(vulkan.device, resources.color_image, &mem_reqs);

	VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	check(vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &resources.color_memory));
	check(vkBindImageMemory(vulkan.device, resources.color_image, resources.color_memory, 0));

	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = resources.color_format;
	view_info.image = resources.color_image;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	check(vkCreateImageView(vulkan.device, &view_info, nullptr, &resources.color_view));
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)resources.color_view, "anki_scene2_color_view");
}

static void create_mock_texture(const vulkan_setup_t& vulkan, Resources& resources)
{
	const std::vector<uint8_t> pixels = make_checker(
		kTextureSize, kTextureSize, {230, 193, 91, 255}, {44, 74, 122, 255}, 8);
	acceleration_structures::Buffer staging = acceleration_structures::prepare_buffer(
		vulkan,
		pixels.size(),
		const_cast<uint8_t*>(pixels.data()),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)staging.handle, "anki_scene2_texture_staging");

	VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = resources.albedo.format;
	image_info.extent = {kTextureSize, kTextureSize, 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	check(vkCreateImage(vulkan.device, &image_info, nullptr, &resources.albedo.image));
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)resources.albedo.image, "anki_scene2_albedo_image");

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(vulkan.device, resources.albedo.image, &mem_reqs);

	VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	check(vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &resources.albedo.memory));
	check(vkBindImageMemory(vulkan.device, resources.albedo.image, resources.albedo.memory, 0));

	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = resources.albedo.format;
	view_info.image = resources.albedo.image;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	check(vkCreateImageView(vulkan.device, &view_info, nullptr, &resources.albedo.view));
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)resources.albedo.view, "anki_scene2_albedo_view");

	VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr};
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.maxLod = 1.0f;
	check(vkCreateSampler(vulkan.device, &sampler_info, nullptr, &resources.albedo.sampler));
	test_set_name(vulkan, VK_OBJECT_TYPE_SAMPLER, (uint64_t)resources.albedo.sampler, "anki_scene2_albedo_sampler");

	VkCommandBuffer command_buffer = resources.context.command_buffer;
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(command_buffer, 0));
	check(vkBeginCommandBuffer(command_buffer, &begin_info));

	VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr};
	to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	to_transfer.srcAccessMask = 0;
	to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	to_transfer.image = resources.albedo.image;
	to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	to_transfer.subresourceRange.levelCount = 1;
	to_transfer.subresourceRange.layerCount = 1;
	vkCmdPipelineBarrier(command_buffer,
			     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			     VK_PIPELINE_STAGE_TRANSFER_BIT,
			     0,
			     0,
			     nullptr,
			     0,
			     nullptr,
			     1,
			     &to_transfer);

	VkBufferImageCopy copy_region{};
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent = {kTextureSize, kTextureSize, 1};
	vkCmdCopyBufferToImage(command_buffer, staging.handle, resources.albedo.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

	VkImageMemoryBarrier to_sample{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr};
	to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	to_sample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	to_sample.image = resources.albedo.image;
	to_sample.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	to_sample.subresourceRange.levelCount = 1;
	to_sample.subresourceRange.layerCount = 1;
	vkCmdPipelineBarrier(command_buffer,
			     VK_PIPELINE_STAGE_TRANSFER_BIT,
			     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			     0,
			     0,
			     nullptr,
			     0,
			     nullptr,
			     1,
			     &to_sample);

	check(vkEndCommandBuffer(command_buffer));

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	check(vkQueueSubmit(resources.context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(resources.context.queue));

	destroy_buffer(vulkan, staging);
}

static void create_render_pass(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkAttachmentDescription color_attachment{};
	color_attachment.format = resources.color_format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_ref{};
	color_ref.attachment = 0;
	color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;

	VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr};
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	check(vkCreateRenderPass(vulkan.device, &render_pass_info, nullptr, &resources.render_pass));
	test_set_name(vulkan, VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)resources.render_pass, "anki_scene2_render_pass");
}

static void create_framebuffer(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkImageView attachments[] = {resources.color_view};

	VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr};
	framebuffer_info.renderPass = resources.render_pass;
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = attachments;
	framebuffer_info.width = kRenderWidth;
	framebuffer_info.height = kRenderHeight;
	framebuffer_info.layers = 1;
	check(vkCreateFramebuffer(vulkan.device, &framebuffer_info, nullptr, &resources.framebuffer));
	test_set_name(vulkan, VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)resources.framebuffer, "anki_scene2_framebuffer");
}

static void create_geometry(const vulkan_setup_t& vulkan, Resources& resources)
{
	resources.vertex_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(QuadVertex) * kQuadVertices.size(),
		const_cast<QuadVertex*>(kQuadVertices.data()),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.vertex_buffer.handle, "anki_scene2_vertex_buffer");

	resources.index_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(uint16_t) * kQuadIndices.size(),
		const_cast<uint16_t*>(kQuadIndices.data()),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.index_buffer.handle, "anki_scene2_index_buffer");
}

static void create_scene_buffers(const vulkan_setup_t& vulkan, Resources& resources)
{
	resources.camera_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(CameraParams),
		nullptr,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.camera_buffer.handle, "anki_scene2_camera");

	resources.material_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(MaterialData),
		nullptr,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	resources.material_buffer.address.deviceAddress =
		acceleration_structures::get_buffer_device_address(vulkan, resources.material_buffer.handle);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.material_buffer.handle, "anki_scene2_material");

	resources.highlight_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(HighlightData),
		nullptr,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	resources.highlight_buffer.address.deviceAddress =
		acceleration_structures::get_buffer_device_address(vulkan, resources.highlight_buffer.handle);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.highlight_buffer.handle, "anki_scene2_highlight");

	resources.scene_pointers_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(ScenePointers),
		nullptr,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.scene_pointers_buffer.handle, "anki_scene2_scene_ptrs");

	resources.frame_config_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(FrameConfig),
		nullptr,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.frame_config_buffer.handle, "anki_scene2_frame_cfg");

	resources.readback_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		kRenderWidth * kRenderHeight * 4,
		nullptr,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resources.readback_buffer.handle, "anki_scene2_readback");
}

static void upload_scene_data(const vulkan_setup_t& vulkan, Resources& resources)
{
	const bool informative = vulkan.has_explicit_host_updates;

	const CameraParams camera{{0.0f, 0.0f, 1.35f, 1.0f}};
	upload_host_visible_struct(vulkan, resources.camera_buffer, camera, informative);

	const MaterialData material{{
		0.85f, 0.92f, 1.00f, 1.00f,
	}, {
		0.35f, 0.80f, 0.00f, 0.00f,
	}};
	upload_host_visible_struct(vulkan, resources.material_buffer, material, informative);

	const HighlightData highlight{{1.00f, 0.55f, 0.15f, 1.00f}};
	upload_host_visible_struct(vulkan, resources.highlight_buffer, highlight, informative);

	const ScenePointers scene_pointers{resources.material_buffer.address.deviceAddress};

	VkMarkingTypeARM marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
	VkMarkingSubTypeARM sub_type{};
	sub_type.deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
	VkDeviceSize marked_offset = 0;
	VkMarkedOffsetsARM markings{VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr};
	markings.count = 1;
	markings.pMarkingTypes = &marking_type;
	markings.pSubTypes = &sub_type;
	markings.pOffsets = &marked_offset;

	upload_host_visible_struct(vulkan,
				       resources.scene_pointers_buffer,
				       scene_pointers,
				       informative,
				       vulkan.has_trace_helpers ? &markings : nullptr);
}

static void create_descriptor_set(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkDescriptorSetLayoutBinding bindings[5]{};

	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[3].binding = 3;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[3].descriptorCount = 1;
	bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[4].binding = 4;
	bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[4].descriptorCount = 1;
	bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	layout_info.bindingCount = 5;
	layout_info.pBindings = bindings;
	check(vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &resources.descriptor_set_layout));
	test_set_name(vulkan, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)resources.descriptor_set_layout, "anki_scene2_dset_layout");

	VkPushConstantRange push_range{};
	push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	push_range.offset = 0;
	push_range.size = sizeof(PushConstants);

	VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &resources.descriptor_set_layout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &push_range;
	check(vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &resources.pipeline_layout));
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)resources.pipeline_layout, "anki_scene2_pipeline_layout");

	VkDescriptorPoolSize pool_sizes[3]{};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[0].descriptorCount = 3;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	pool_sizes[1].descriptorCount = 1;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[2].descriptorCount = 1;

	VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr};
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 3;
	pool_info.pPoolSizes = pool_sizes;
	check(vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &resources.descriptor_pool));

	VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr};
	alloc_info.descriptorPool = resources.descriptor_pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &resources.descriptor_set_layout;
	check(vkAllocateDescriptorSets(vulkan.device, &alloc_info, &resources.descriptor_set));

	VkDescriptorBufferInfo camera_info{};
	camera_info.buffer = resources.camera_buffer.handle;
	camera_info.range = sizeof(CameraParams);

	VkDescriptorBufferInfo scene_info{};
	scene_info.buffer = resources.scene_pointers_buffer.handle;
	scene_info.range = sizeof(ScenePointers);

	VkDescriptorBufferInfo frame_info{};
	frame_info.buffer = resources.frame_config_buffer.handle;
	frame_info.range = sizeof(FrameConfig);

	VkDescriptorImageInfo image_info{};
	image_info.sampler = resources.albedo.sampler;
	image_info.imageView = resources.albedo.view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSetAccelerationStructureKHR as_info{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, nullptr};
	as_info.accelerationStructureCount = 1;
	as_info.pAccelerationStructures = &resources.accel.tlas.handle;

	VkWriteDescriptorSet writes[5]{};

	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = resources.descriptor_set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].pBufferInfo = &camera_info;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].pNext = &as_info;
	writes[1].dstSet = resources.descriptor_set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet = resources.descriptor_set;
	writes[2].dstBinding = 2;
	writes[2].descriptorCount = 1;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[2].pImageInfo = &image_info;

	writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[3].dstSet = resources.descriptor_set;
	writes[3].dstBinding = 3;
	writes[3].descriptorCount = 1;
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[3].pBufferInfo = &scene_info;

	writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[4].dstSet = resources.descriptor_set;
	writes[4].dstBinding = 4;
	writes[4].descriptorCount = 1;
	writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[4].pBufferInfo = &frame_info;

	vkUpdateDescriptorSets(vulkan.device, 5, writes, 0, nullptr);
}

static void create_pipeline(const vulkan_setup_t& vulkan, Resources& resources)
{
	resources.shader_stages[0] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_anki_scene2_vert_spv, vulkan_anki_scene2_vert_spv_len, VK_SHADER_STAGE_VERTEX_BIT);
	resources.shader_stages[1] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_anki_scene2_frag_spv, vulkan_anki_scene2_frag_spv_len, VK_SHADER_STAGE_FRAGMENT_BIT);

	VkVertexInputBindingDescription binding{};
	binding.binding = 0;
	binding.stride = sizeof(QuadVertex);
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attributes[2]{};
	attributes[0].location = 0;
	attributes[0].binding = 0;
	attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
	attributes[0].offset = offsetof(QuadVertex, pos);

	attributes[1].location = 1;
	attributes[1].binding = 0;
	attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
	attributes[1].offset = offsetof(QuadVertex, uv);

	VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr};
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &binding;
	vertex_input.vertexAttributeDescriptionCount = 2;
	vertex_input.pVertexAttributeDescriptions = attributes;

	VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{};
	viewport.width = static_cast<float>(kRenderWidth);
	viewport.height = static_cast<float>(kRenderHeight);
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.extent = {kRenderWidth, kRenderHeight};

	VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr};
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr};
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState color_blend_attachment{};
	color_blend_attachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr};
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &color_blend_attachment;

	VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr};
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = resources.shader_stages;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.layout = resources.pipeline_layout;
	pipeline_info.renderPass = resources.render_pass;
	check(vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &resources.pipeline));
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)resources.pipeline, "anki_scene2_pipeline");
}

static void draw_and_readback(const vulkan_setup_t& vulkan, Resources& resources)
{
	const FrameConfig frame_config{{1.05f, 0.25f, 4.0f, 4.0f}};
	const PushConstants push_constants{resources.highlight_buffer.address.deviceAddress, 0.30f, {0.0f, 0.0f, 0.0f}};

	VkCommandBuffer command_buffer = resources.context.command_buffer;
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(command_buffer, 0));
	check(vkBeginCommandBuffer(command_buffer, &begin_info));

	if (vulkan.vkCmdUpdateBuffer2)
	{
		VkUpdateBufferInfoARM update_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr};
		update_info.dstBuffer = resources.frame_config_buffer.handle;
		update_info.dstOffset = 0;
		update_info.dataSize = sizeof(FrameConfig);
		update_info.pData = &frame_config;
		vulkan.vkCmdUpdateBuffer2(command_buffer, &update_info);
	}
	else
	{
		vkCmdUpdateBuffer(command_buffer, resources.frame_config_buffer.handle, 0, sizeof(FrameConfig), &frame_config);
	}

	VkBufferMemoryBarrier frame_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
	frame_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	frame_barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
	frame_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	frame_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	frame_barrier.buffer = resources.frame_config_buffer.handle;
	frame_barrier.size = sizeof(FrameConfig);
	vkCmdPipelineBarrier(command_buffer,
			     VK_PIPELINE_STAGE_TRANSFER_BIT,
			     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			     0,
			     0,
			     nullptr,
			     1,
			     &frame_barrier,
			     0,
			     nullptr);

	VkClearValue clear_value{};
	clear_value.color.float32[0] = 0.04f;
	clear_value.color.float32[1] = 0.06f;
	clear_value.color.float32[2] = 0.09f;
	clear_value.color.float32[3] = 1.0f;

	VkRenderPassBeginInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr};
	render_pass_info.renderPass = resources.render_pass;
	render_pass_info.framebuffer = resources.framebuffer;
	render_pass_info.renderArea.extent = {kRenderWidth, kRenderHeight};
	render_pass_info.clearValueCount = 1;
	render_pass_info.pClearValues = &clear_value;

	vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipeline);
	vkCmdBindDescriptorSets(command_buffer,
			       VK_PIPELINE_BIND_POINT_GRAPHICS,
			       resources.pipeline_layout,
			       0,
			       1,
			       &resources.descriptor_set,
			       0,
			       nullptr);

	VkMarkingTypeARM marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
	VkMarkingSubTypeARM sub_type{};
	sub_type.deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
	VkDeviceSize marked_offset = 0;
	VkMarkedOffsetsARM markings{VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr};
	markings.count = 1;
	markings.pMarkingTypes = &marking_type;
	markings.pSubTypes = &sub_type;
	markings.pOffsets = &marked_offset;

	if (vulkan.vkCmdPushConstants2)
	{
		VkPushConstantsInfoKHR push_info{VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR, nullptr};
		push_info.layout = resources.pipeline_layout;
		push_info.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		push_info.offset = 0;
		push_info.size = sizeof(PushConstants);
		push_info.pValues = &push_constants;
		if (vulkan.has_trace_helpers)
		{
			push_info.pNext = &markings;
		}
		vulkan.vkCmdPushConstants2(command_buffer, &push_info);
	}
	else
	{
		assert(!vulkan.has_trace_helpers);
		vkCmdPushConstants(command_buffer, resources.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push_constants);
	}

	VkDeviceSize vertex_offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &resources.vertex_buffer.handle, &vertex_offset);
	vkCmdBindIndexBuffer(command_buffer, resources.index_buffer.handle, 0, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(command_buffer, static_cast<uint32_t>(kQuadIndices.size()), 1, 0, 0, 0);
	vkCmdEndRenderPass(command_buffer);

	VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr};
	to_transfer.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	to_transfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	to_transfer.image = resources.color_image;
	to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	to_transfer.subresourceRange.levelCount = 1;
	to_transfer.subresourceRange.layerCount = 1;
	vkCmdPipelineBarrier(command_buffer,
			     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			     VK_PIPELINE_STAGE_TRANSFER_BIT,
			     0,
			     0,
			     nullptr,
			     0,
			     nullptr,
			     1,
			     &to_transfer);

	VkBufferImageCopy copy_region{};
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent = {kRenderWidth, kRenderHeight, 1};
	vkCmdCopyImageToBuffer(command_buffer,
			       resources.color_image,
			       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			       resources.readback_buffer.handle,
			       1,
			       &copy_region);

	VkBufferMemoryBarrier readback_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
	readback_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	readback_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	readback_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	readback_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	readback_barrier.buffer = resources.readback_buffer.handle;
	readback_barrier.size = VK_WHOLE_SIZE;
	vkCmdPipelineBarrier(command_buffer,
			     VK_PIPELINE_STAGE_TRANSFER_BIT,
			     VK_PIPELINE_STAGE_HOST_BIT,
			     0,
			     0,
			     nullptr,
			     1,
			     &readback_barrier,
			     0,
			     nullptr);

	check(vkEndCommandBuffer(command_buffer));

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	check(vkQueueSubmit(resources.context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(resources.context.queue));
}

static void verify_output(const vulkan_setup_t& vulkan, const Resources& resources)
{
	void* mapped = nullptr;
	check(vkMapMemory(vulkan.device, resources.readback_buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped));

	const uint8_t* bytes = static_cast<const uint8_t*>(mapped);
	bool found_non_clear = false;
	for (size_t i = 0; i < 64 && i < (kRenderWidth * kRenderHeight * 4); i += 4)
	{
		if (bytes[i] != 10 || bytes[i + 1] != 15 || bytes[i + 2] != 23)
		{
			found_non_clear = true;
			break;
		}
	}
	assert(found_non_clear && "Expected the ray query pass to modify at least one pixel");
	vkUnmapMemory(vulkan.device, resources.readback_buffer.memory);

	if (vulkan.vkAssertBuffer)
	{
		uint32_t checksum = 0;
		const VkUpdateBufferInfoARM readback_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, resources.readback_buffer.handle, 0, VK_WHOLE_SIZE, nullptr};
		VkResult result = vulkan.vkAssertBuffer(
			vulkan.device, &readback_info, &checksum, "anki scene2 color readback");
		check(result);
		printf("anki_scene2 checksum: 0x%08x\n", checksum);
	}
}

static void cleanup(const vulkan_setup_t& vulkan, Resources& resources)
{
	vkDestroyPipeline(vulkan.device, resources.pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, resources.pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, resources.descriptor_set_layout, nullptr);
	vkDestroyDescriptorPool(vulkan.device, resources.descriptor_pool, nullptr);

	for (auto& stage : resources.shader_stages)
	{
		if (stage.module != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(vulkan.device, stage.module, nullptr);
			stage.module = VK_NULL_HANDLE;
		}
	}

	vkDestroyFramebuffer(vulkan.device, resources.framebuffer, nullptr);
	vkDestroyRenderPass(vulkan.device, resources.render_pass, nullptr);

	vkDestroyImageView(vulkan.device, resources.color_view, nullptr);
	vkDestroyImage(vulkan.device, resources.color_image, nullptr);
	vkFreeMemory(vulkan.device, resources.color_memory, nullptr);

	vkDestroySampler(vulkan.device, resources.albedo.sampler, nullptr);
	vkDestroyImageView(vulkan.device, resources.albedo.view, nullptr);
	vkDestroyImage(vulkan.device, resources.albedo.image, nullptr);
	vkFreeMemory(vulkan.device, resources.albedo.memory, nullptr);

	destroy_buffer(vulkan, resources.vertex_buffer);
	destroy_buffer(vulkan, resources.index_buffer);
	destroy_buffer(vulkan, resources.camera_buffer);
	destroy_buffer(vulkan, resources.material_buffer);
	destroy_buffer(vulkan, resources.highlight_buffer);
	destroy_buffer(vulkan, resources.scene_pointers_buffer);
	destroy_buffer(vulkan, resources.frame_config_buffer);
	destroy_buffer(vulkan, resources.readback_buffer);

	ray_tracing_common::destroy_simple_triangle_as(vulkan, resources.context, resources.accel);
	ray_tracing_common::destroy_context(vulkan, resources.context);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray_query_features};

	ray_query_features.rayQuery = VK_TRUE;
	acceleration_features.accelerationStructure = VK_TRUE;

	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&acceleration_features);
	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_MAINTENANCE_6_EXTENSION_NAME);
	reqs.options["image_output"] = false;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_anki_scene2", reqs);

	VkPhysicalDeviceRayQueryFeaturesKHR rq_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as_supported{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq_supported};
	VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &as_supported};
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &supported);
	assert(rq_supported.rayQuery && "Ray query feature required");
	assert(as_supported.accelerationStructure && "Acceleration structure feature required");
	assert(vulkan.hasfeat12.bufferDeviceAddress && "Buffer device address required");
	assert(vulkan.vkCmdPushConstants2 && "vkCmdPushConstants2KHR is required for this test");

	Resources resources{};
	ray_tracing_common::init_context(vulkan, resources.context);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)resources.context.command_pool, "anki_scene2_command_pool");
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)resources.context.command_buffer, "anki_scene2_command_buffer");

	test_marker(vulkan, "AnkiScene2: build mock scene acceleration structure");
	ray_tracing_common::build_simple_triangle_as(vulkan, resources.context, resources.accel);
	test_marker_mention(vulkan, "AnkiScene2: TLAS ready", VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (uint64_t)resources.accel.tlas.handle);

	create_color_target(vulkan, resources);
	create_mock_texture(vulkan, resources);
	create_render_pass(vulkan, resources);
	create_framebuffer(vulkan, resources);
	create_geometry(vulkan, resources);
	create_scene_buffers(vulkan, resources);
	upload_scene_data(vulkan, resources);
	create_descriptor_set(vulkan, resources);
	create_pipeline(vulkan, resources);

	test_marker(vulkan, "AnkiScene2: RqReflectionsUber mock draw");
	bench_start_iteration(vulkan.bench);
	draw_and_readback(vulkan, resources);
	bench_stop_iteration(vulkan.bench);

	verify_output(vulkan, resources);
	if (std::get<bool>(reqs.options.at("image_output")))
	{
		test_save_image(vulkan, "anki_scene2.png", resources.readback_buffer.memory, 0, kRenderWidth, kRenderHeight, resources.color_format);
	}

	cleanup(vulkan, resources);
	test_done(vulkan);
	return 0;
}
