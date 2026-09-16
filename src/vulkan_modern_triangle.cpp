// Headless coloured triangle using Vulkan 1.4-era interfaces only.

#include "vulkan_common.h"

#include "vulkan_modern_triangle_vert.inc"
#include "vulkan_modern_triangle_frag.inc"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

static bool use_device_address_commands = false;
static VkPhysicalDeviceShaderObjectFeaturesEXT shader_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT, nullptr, VK_TRUE };
static VkPhysicalDeviceDescriptorHeapFeaturesEXT heap_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT, &shader_features, VK_TRUE, VK_FALSE };
static VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR address_command_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_ADDRESS_COMMANDS_FEATURES_KHR, &heap_features, VK_TRUE };

struct buffer_allocation
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceAddress address = 0;
	VkDeviceSize size = 0;
	void* mapped = nullptr;
};

struct image_allocation
{
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

static void show_usage()
{
	printf("-i/--image-output      Save the rendered image to modern_triangle.png\n");
	printf("-W/--width N           Output width (default 640)\n");
	printf("-H/--height N          Output height (default 480)\n");
	printf("-fb/--frame-boundary   Publish the output through VK_EXT_frame_boundary\n");
	printf("-ac/--address-commands Use device address commands\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-i", "--image-output"))
	{
		reqs.options["image_output"] = true;
		return true;
	}
	if (match(argv[i], "-W", "--width"))
	{
		reqs.options["width"] = get_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-H", "--height"))
	{
		reqs.options["height"] = get_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-fb", "--frame-boundary"))
	{
		return enable_frame_boundary(reqs);
	}
	if (match(argv[i], "-ac", "--address-commands"))
	{
		if (!use_device_address_commands)
		{
			use_device_address_commands = true;
			reqs.device_extensions.push_back(VK_KHR_DEVICE_ADDRESS_COMMANDS_EXTENSION_NAME);
			address_command_features.pNext = reqs.extension_features;
			reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&address_command_features);
		}
		return true;
	}
	return false;
}

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
	if (alignment == 0) return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static VkDeviceAddress align_up_address(VkDeviceAddress value, VkDeviceSize alignment)
{
	if (alignment == 0) return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static buffer_allocation create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size,
	                                    VkBufferUsageFlags usage, const char* name)
{
	buffer_allocation out{};
	out.size = size;

	VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	buffer_info.size = size;
	buffer_info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &out.buffer);
	check(result);

	VkMemoryRequirements requirements{};
	vkGetBufferMemoryRequirements(vulkan.device, out.buffer, &requirements);
	VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr};
	flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags_info};
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(
		requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &out.memory);
	check(result);
	result = vkBindBufferMemory(vulkan.device, out.buffer, out.memory, 0);
	check(result);
	result = vkMapMemory(vulkan.device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped);
	check(result);
	assert(out.mapped);

	VkBufferDeviceAddressInfo address_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr};
	address_info.buffer = out.buffer;
	out.address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
	assert(out.address != 0);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)out.buffer, name);
	return out;
}

static image_allocation create_image(const vulkan_setup_t& vulkan, uint32_t width, uint32_t height)
{
	image_allocation out{};
	VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent = {width, height, 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult result = vkCreateImage(vulkan.device, &image_info, nullptr, &out.image);
	check(result);

	VkMemoryRequirements requirements{};
	vkGetImageMemoryRequirements(vulkan.device, out.image, &requirements);
	VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &out.memory);
	check(result);
	result = vkBindImageMemory(vulkan.device, out.image, out.memory, 0);
	check(result);

	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	view_info.image = out.image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = image_info.format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	result = vkCreateImageView(vulkan.device, &view_info, nullptr, &out.view);
	check(result);
	return out;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_4;
	reqs.minApiVersion = VK_API_VERSION_1_4;
	reqs.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT;
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat13.dynamicRendering = VK_TRUE;
	reqs.options["width"] = 640;
	reqs.options["height"] = 480;
	reqs.device_extensions.push_back(VK_EXT_SHADER_OBJECT_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&heap_features);
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_modern_triangle", reqs);
	const uint32_t width = std::get<int>(reqs.options.at("width"));
	const uint32_t height = std::get<int>(reqs.options.at("height"));
	assert(width > 0 && height > 0);

	MAKEDEVICEPROCADDR(vulkan, vkCreateShadersEXT);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyShaderEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBindShadersEXT);
	MAKEDEVICEPROCADDR(vulkan, vkWriteResourceDescriptorsEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBindResourceHeapEXT);
	PFN_vkCmdBindVertexBuffers3KHR pf_vkCmdBindVertexBuffers3KHR = nullptr;
	PFN_vkCmdCopyImageToMemoryKHR pf_vkCmdCopyImageToMemoryKHR = nullptr;
	if (use_device_address_commands)
	{
		pf_vkCmdBindVertexBuffers3KHR = reinterpret_cast<PFN_vkCmdBindVertexBuffers3KHR>(
			vkGetDeviceProcAddr(vulkan.device, "vkCmdBindVertexBuffers3KHR"));
		pf_vkCmdCopyImageToMemoryKHR = reinterpret_cast<PFN_vkCmdCopyImageToMemoryKHR>(
			vkGetDeviceProcAddr(vulkan.device, "vkCmdCopyImageToMemoryKHR"));
		assert(pf_vkCmdBindVertexBuffers3KHR);
		assert(pf_vkCmdCopyImageToMemoryKHR);
	}
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetVertexInputEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetDepthClampEnableEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetPolygonModeEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetRasterizationSamplesEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetSampleMaskEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetAlphaToCoverageEnableEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetAlphaToOneEnableEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetLogicOpEnableEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetColorBlendEnableEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetColorBlendEquationEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetColorWriteMaskEXT);

	const std::array<std::array<float, 4>, 3> colors = {{
		{{1.0f, 0.0f, 0.0f, 1.0f}},
		{{0.0f, 1.0f, 0.0f, 1.0f}},
		{{0.0f, 0.0f, 1.0f, 1.0f}},
	}};
	const std::array<std::array<float, 2>, 3> positions = {{
		{{0.0f, -0.75f}},
		{{0.75f, 0.75f}},
		{{-0.75f, 0.75f}},
	}};
	buffer_allocation position_buffer = create_buffer(
		vulkan, sizeof(positions), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "modern_triangle_positions");
	std::memcpy(position_buffer.mapped, positions.data(), sizeof(positions));
	testFlushMemory(vulkan, position_buffer.memory, 0, VK_WHOLE_SIZE);
	buffer_allocation color_buffer = create_buffer(
		vulkan, sizeof(colors), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "modern_triangle_colors");
	std::memcpy(color_buffer.mapped, colors.data(), sizeof(colors));
	testFlushMemory(vulkan, color_buffer.memory, 0, VK_WHOLE_SIZE);

	VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_properties{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT, nullptr};
	VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &heap_properties};
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties);
	assert(heap_properties.bufferDescriptorSize > 0);

	const VkDeviceSize reserved_offset = align_up(
		heap_properties.bufferDescriptorSize, heap_properties.resourceHeapAlignment);
	const VkDeviceSize heap_size = reserved_offset + heap_properties.minResourceHeapReservedRange;
	assert(heap_size <= heap_properties.maxResourceHeapSize);
	const VkDeviceSize heap_allocation_size = heap_size + std::max(heap_properties.resourceHeapAlignment, VkDeviceSize(1)) - 1;
	buffer_allocation resource_heap = create_buffer(
		vulkan, heap_allocation_size, VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT, "modern_triangle_resource_heap");
	const VkDeviceAddress heap_base = align_up_address(resource_heap.address, heap_properties.resourceHeapAlignment);
	const VkDeviceSize heap_map_offset = heap_base - resource_heap.address;
	assert(heap_map_offset + heap_size <= resource_heap.size);

	VkDeviceAddressRangeEXT color_range{color_buffer.address, color_buffer.size};
	VkResourceDescriptorInfoEXT color_descriptor{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT, nullptr};
	color_descriptor.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	color_descriptor.data.pAddressRange = &color_range;
	VkHostAddressRangeEXT descriptor_output{};
	descriptor_output.address = static_cast<uint8_t*>(resource_heap.mapped) + heap_map_offset;
	descriptor_output.size = static_cast<size_t>(heap_properties.bufferDescriptorSize);
	VkResult result = pf_vkWriteResourceDescriptorsEXT(
		vulkan.device, 1, &color_descriptor, &descriptor_output);
	check(result);
	testFlushMemoryDescriptors(vulkan, resource_heap.memory, 0, resource_heap.size,
	                           {heap_map_offset}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});

	VkDescriptorSetAndBindingMappingEXT mapping{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT, nullptr};
	mapping.descriptorSet = 0;
	mapping.firstBinding = 0;
	mapping.bindingCount = 1;
	mapping.resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT;
	mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
	mapping.sourceData.constantOffset.heapOffset = 0;
	VkShaderDescriptorSetAndBindingMappingInfoEXT mapping_info{
		VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT, nullptr, 1, &mapping};

	std::array<VkShaderCreateInfoEXT, 2> shader_infos{};
	shader_infos[0].sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
	shader_infos[0].pNext = &mapping_info;
	shader_infos[0].flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT | VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT;
	shader_infos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_infos[0].nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_infos[0].codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
	shader_infos[0].codeSize = vulkan_modern_triangle_vert_spirv_len;
	shader_infos[0].pCode = vulkan_modern_triangle_vert_spirv;
	shader_infos[0].pName = "main";
	shader_infos[1].sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
	shader_infos[1].flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT | VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT;
	shader_infos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_infos[1].codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
	shader_infos[1].codeSize = vulkan_modern_triangle_frag_spirv_len;
	shader_infos[1].pCode = vulkan_modern_triangle_frag_spirv;
	shader_infos[1].pName = "main";
	std::array<VkShaderEXT, 2> shaders{};
	result = pf_vkCreateShadersEXT(vulkan.device, shaders.size(), shader_infos.data(), nullptr, shaders.data());
	check(result);

	image_allocation target = create_image(vulkan, width, height);
	const VkDeviceSize readback_size = VkDeviceSize(width) * height * 4;
	buffer_allocation readback = create_buffer(
		vulkan, readback_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "modern_triangle_readback");
	std::memset(readback.mapped, 0, static_cast<size_t>(readback_size));

	VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
	check(result);
	VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	command_info.commandPool = command_pool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &command_info, &command_buffer);
	check(result);
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkImageMemoryBarrier2 to_color{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr};
	to_color.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	to_color.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	to_color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	to_color.image = target.image;
	to_color.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	to_color.subresourceRange.levelCount = 1;
	to_color.subresourceRange.layerCount = 1;
	VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr};
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &to_color;
	vkCmdPipelineBarrier2(command_buffer, &dependency);

	VkClearValue clear{};
	clear.color = {{0.05f, 0.05f, 0.05f, 1.0f}};
	VkRenderingAttachmentInfo color_attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr};
	color_attachment.imageView = target.view;
	color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.clearValue = clear;
	VkRenderingInfo rendering_info{VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr};
	rendering_info.renderArea.extent = {width, height};
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &color_attachment;
	vkCmdBeginRendering(command_buffer, &rendering_info);

	const std::array<VkShaderStageFlagBits, 2> stages = {
		VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
	pf_vkCmdBindShadersEXT(command_buffer, stages.size(), stages.data(), shaders.data());
	VkBindHeapInfoEXT heap_bind{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT, nullptr};
	heap_bind.heapRange = {heap_base, heap_size};
	heap_bind.reservedRangeOffset = reserved_offset;
	heap_bind.reservedRangeSize = heap_properties.minResourceHeapReservedRange;
	pf_vkCmdBindResourceHeapEXT(command_buffer, &heap_bind);

	VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
	VkRect2D scissor{{0, 0}, {width, height}};
	vkCmdSetViewportWithCount(command_buffer, 1, &viewport);
	vkCmdSetScissorWithCount(command_buffer, 1, &scissor);
	vkCmdSetCullMode(command_buffer, VK_CULL_MODE_NONE);
	vkCmdSetFrontFace(command_buffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	vkCmdSetPrimitiveTopology(command_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	vkCmdSetRasterizerDiscardEnable(command_buffer, VK_FALSE);
	vkCmdSetDepthBiasEnable(command_buffer, VK_FALSE);
	vkCmdSetPrimitiveRestartEnable(command_buffer, VK_FALSE);
	vkCmdSetDepthTestEnable(command_buffer, VK_FALSE);
	vkCmdSetDepthWriteEnable(command_buffer, VK_FALSE);
	vkCmdSetDepthCompareOp(command_buffer, VK_COMPARE_OP_ALWAYS);
	vkCmdSetStencilTestEnable(command_buffer, VK_FALSE);
	VkVertexInputBindingDescription2EXT vertex_binding{
		VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT, nullptr};
	vertex_binding.binding = 0;
	vertex_binding.stride = sizeof(positions[0]);
	vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertex_binding.divisor = 1;
	VkVertexInputAttributeDescription2EXT vertex_attribute{
		VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr};
	vertex_attribute.location = 0;
	vertex_attribute.binding = 0;
	vertex_attribute.format = VK_FORMAT_R32G32_SFLOAT;
	pf_vkCmdSetVertexInputEXT(command_buffer, 1, &vertex_binding, 1, &vertex_attribute);
	pf_vkCmdSetDepthClampEnableEXT(command_buffer, VK_FALSE);
	pf_vkCmdSetPolygonModeEXT(command_buffer, VK_POLYGON_MODE_FILL);
	pf_vkCmdSetRasterizationSamplesEXT(command_buffer, VK_SAMPLE_COUNT_1_BIT);
	VkSampleMask sample_mask = 1;
	pf_vkCmdSetSampleMaskEXT(command_buffer, VK_SAMPLE_COUNT_1_BIT, &sample_mask);
	pf_vkCmdSetAlphaToCoverageEnableEXT(command_buffer, VK_FALSE);
	pf_vkCmdSetAlphaToOneEnableEXT(command_buffer, VK_FALSE);
	pf_vkCmdSetLogicOpEnableEXT(command_buffer, VK_FALSE);
	VkBool32 blend_enable = VK_FALSE;
	pf_vkCmdSetColorBlendEnableEXT(command_buffer, 0, 1, &blend_enable);
	VkColorBlendEquationEXT blend_equation{};
	blend_equation.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_equation.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	blend_equation.colorBlendOp = VK_BLEND_OP_ADD;
	blend_equation.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_equation.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blend_equation.alphaBlendOp = VK_BLEND_OP_ADD;
	pf_vkCmdSetColorBlendEquationEXT(command_buffer, 0, 1, &blend_equation);
	VkColorComponentFlags color_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	pf_vkCmdSetColorWriteMaskEXT(command_buffer, 0, 1, &color_mask);

	if (use_device_address_commands)
	{
		VkBindVertexBuffer3InfoKHR vertex_bind{VK_STRUCTURE_TYPE_BIND_VERTEX_BUFFER_3_INFO_KHR, nullptr};
		vertex_bind.setStride = VK_TRUE;
		vertex_bind.addressRange = {position_buffer.address, position_buffer.size, sizeof(positions[0])};
		pf_vkCmdBindVertexBuffers3KHR(command_buffer, 0, 1, &vertex_bind);
	}
	else
	{
		VkDeviceSize vertex_offset = 0;
		vkCmdBindVertexBuffers(command_buffer, 0, 1, &position_buffer.buffer, &vertex_offset);
	}
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	vkCmdEndRendering(command_buffer);

	VkImageMemoryBarrier2 to_copy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr};
	to_copy.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	to_copy.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	to_copy.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	to_copy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	to_copy.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	to_copy.image = target.image;
	to_copy.subresourceRange = to_color.subresourceRange;
	dependency.pImageMemoryBarriers = &to_copy;
	vkCmdPipelineBarrier2(command_buffer, &dependency);

	if (use_device_address_commands)
	{
		VkDeviceMemoryImageCopyKHR copy_region{VK_STRUCTURE_TYPE_DEVICE_MEMORY_IMAGE_COPY_KHR, nullptr};
		copy_region.addressRange = {readback.address, readback_size};
		copy_region.addressFlags = 0;
		copy_region.addressRowLength = width;
		copy_region.addressImageHeight = height;
		copy_region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy_region.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		copy_region.imageExtent = {width, height, 1};
		VkCopyDeviceMemoryImageInfoKHR copy_info{
			VK_STRUCTURE_TYPE_COPY_DEVICE_MEMORY_IMAGE_INFO_KHR, nullptr, target.image, 1, &copy_region};
		pf_vkCmdCopyImageToMemoryKHR(command_buffer, &copy_info);
	}
	else
	{
		VkBufferImageCopy copy_region{};
		copy_region.bufferRowLength = width;
		copy_region.bufferImageHeight = height;
		copy_region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy_region.imageExtent = {width, height, 1};
		vkCmdCopyImageToBuffer(command_buffer, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                       readback.buffer, 1, &copy_region);
	}

	VkMemoryBarrier2 host_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr};
	host_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	host_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	host_barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
	host_barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
	dependency.imageMemoryBarrierCount = 0;
	dependency.pImageMemoryBarriers = nullptr;
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers = &host_barrier;
	vkCmdPipelineBarrier2(command_buffer, &dependency);
	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkFrameBoundaryEXT frame_boundary{VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT, nullptr};
	if (reqs.options.count("frame_boundary"))
	{
		frame_boundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
		frame_boundary.frameID = 0;
		frame_boundary.imageCount = 1;
		frame_boundary.pImages = &target.image;
	}
	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.pNext = reqs.options.count("frame_boundary") ? &frame_boundary : nullptr;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	bench_start_iteration(vulkan.bench);
	result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(queue);
	check(result);
	bench_stop_iteration(vulkan.bench);

	if (vulkan.vkAssertBuffer)
	{
		uint32_t checksum = 0;
		VkUpdateBufferInfoARM assert_info{
			VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, readback.buffer, 0, readback_size, nullptr};
		result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum, "modern triangle readback");
		check(result);
	}
	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		const uint8_t* pixels = static_cast<const uint8_t*>(readback.mapped);
		const VkDeviceSize center = (VkDeviceSize(height / 2) * width + width / 2) * 4;
		assert(pixels[center] + pixels[center + 1] + pixels[center + 2] > 100);
	}
	if (reqs.options.count("image_output"))
	{
		vkUnmapMemory(vulkan.device, readback.memory);
		readback.mapped = nullptr;
		test_save_image(vulkan, "modern_triangle.png", readback.memory, 0, width, height, VK_FORMAT_R8G8B8A8_UNORM);
		bench_stop_scene(vulkan.bench, "modern_triangle.png");
	}
	else
	{
		bench_stop_scene(vulkan.bench);
	}

	pf_vkDestroyShaderEXT(vulkan.device, shaders[1], nullptr);
	pf_vkDestroyShaderEXT(vulkan.device, shaders[0], nullptr);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyImageView(vulkan.device, target.view, nullptr);
	vkDestroyImage(vulkan.device, target.image, nullptr);
	testFreeMemory(vulkan, target.memory);
	if (readback.mapped) vkUnmapMemory(vulkan.device, readback.memory);
	vkDestroyBuffer(vulkan.device, readback.buffer, nullptr);
	testFreeMemory(vulkan, readback.memory);
	vkUnmapMemory(vulkan.device, resource_heap.memory);
	vkDestroyBuffer(vulkan.device, resource_heap.buffer, nullptr);
	testFreeMemory(vulkan, resource_heap.memory);
	vkUnmapMemory(vulkan.device, color_buffer.memory);
	vkDestroyBuffer(vulkan.device, color_buffer.buffer, nullptr);
	testFreeMemory(vulkan, color_buffer.memory);
	vkUnmapMemory(vulkan.device, position_buffer.memory);
	vkDestroyBuffer(vulkan.device, position_buffer.buffer, nullptr);
	testFreeMemory(vulkan, position_buffer.memory);
	test_done(vulkan);
	return 0;
}
