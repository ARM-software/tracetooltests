// Test interleaving many small buffers and optimal images in one memory allocation.

#include "vulkan_common.h"

#include <numeric>

namespace
{

constexpr uint32_t kPairCount = 24;
constexpr VkDeviceSize kBufferSize = 256;
constexpr uint32_t kImageWidth = 8;
constexpr uint32_t kImageHeight = 8;
constexpr VkFormat kImageFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct memory_info_t
{
	VkMemoryRequirements requirements = {};
	bool requires_dedicated = false;
};

struct buffer_resource_t
{
	VkBuffer buffer = VK_NULL_HANDLE;
	memory_info_t memory_info = {};
	VkDeviceSize memory_offset = 0;
	uint32_t fill_value = 0;
	uint32_t expected_crc = 0;
};

struct image_resource_t
{
	VkImage image = VK_NULL_HANDLE;
	memory_info_t memory_info = {};
	VkDeviceSize memory_offset = 0;
	VkClearColorValue clear_value = {};
};

memory_info_t get_buffer_memory_info(const vulkan_setup_t& vulkan, VkBuffer buffer)
{
	VkBufferMemoryRequirementsInfo2 info = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2, nullptr };
	info.buffer = buffer;
	VkMemoryDedicatedRequirements dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS, nullptr };
	VkMemoryRequirements2 requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicated };
	vkGetBufferMemoryRequirements2(vulkan.device, &info, &requirements);

	memory_info_t out = {};
	out.requirements = requirements.memoryRequirements;
	out.requires_dedicated = dedicated.requiresDedicatedAllocation == VK_TRUE;
	return out;
}

memory_info_t get_image_memory_info(const vulkan_setup_t& vulkan, VkImage image)
{
	VkImageMemoryRequirementsInfo2 info = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, nullptr };
	info.image = image;
	VkMemoryDedicatedRequirements dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS, nullptr };
	VkMemoryRequirements2 requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicated };
	vkGetImageMemoryRequirements2(vulkan.device, &info, &requirements);

	memory_info_t out = {};
	out.requirements = requirements.memoryRequirements;
	out.requires_dedicated = dedicated.requiresDedicatedAllocation == VK_TRUE;
	return out;
}

VkDeviceSize place_resource(VkDeviceSize allocation_size, const VkMemoryRequirements& requirements, bool previous_is_linear,
                            bool current_is_linear, bool has_previous, VkDeviceSize buffer_image_granularity)
{
	VkDeviceSize alignment = requirements.alignment;
	if (alignment == 0) alignment = 1;
	if (has_previous && previous_is_linear != current_is_linear)
	{
		const VkDeviceSize granularity = buffer_image_granularity == 0 ? 1 : buffer_image_granularity;
		alignment = std::lcm(alignment, granularity);
	}
	return aligned_size(allocation_size, alignment);
}

uint32_t choose_memory_type(const VkPhysicalDeviceMemoryProperties& memory_properties, uint32_t memory_type_bits)
{
	assert(memory_type_bits != 0);
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
	{
		if ((memory_type_bits & (1u << i)) == 0) continue;
		if (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) return i;
	}
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
	{
		if (memory_type_bits & (1u << i)) return i;
	}
	assert(false);
	return 0;
}

buffer_resource_t create_buffer(const vulkan_setup_t& vulkan, uint32_t index)
{
	buffer_resource_t resource = {};
	VkBufferCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	create_info.size = kBufferSize;
	create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &create_info, nullptr, &resource.buffer);
	check(result);
	assert(resource.buffer != VK_NULL_HANDLE);

	std::string name = "linear_optimal_mix_buffer_" + std::to_string(index);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resource.buffer, name.c_str());

	resource.memory_info = get_buffer_memory_info(vulkan, resource.buffer);

	const uint8_t fill_byte = static_cast<uint8_t>(0x20 + index * 5);
	resource.fill_value = uint32_t(fill_byte) * 0x01010101u;
	std::vector<uint8_t> expected(kBufferSize, fill_byte);
	resource.expected_crc = adler32(expected.data(), expected.size());
	return resource;
}

image_resource_t create_image(const vulkan_setup_t& vulkan, uint32_t index)
{
	image_resource_t resource = {};
	VkImageCreateInfo create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	create_info.imageType = VK_IMAGE_TYPE_2D;
	create_info.format = kImageFormat;
	create_info.extent = { kImageWidth, kImageHeight, 1 };
	create_info.mipLevels = 1;
	create_info.arrayLayers = 1;
	create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage(vulkan.device, &create_info, nullptr, &resource.image);
	check(result);
	assert(resource.image != VK_NULL_HANDLE);

	std::string name = "linear_optimal_mix_image_" + std::to_string(index);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)resource.image, name.c_str());

	resource.memory_info = get_image_memory_info(vulkan, resource.image);

	const float base = float(index + 1) / float(kPairCount + 1);
	resource.clear_value.float32[0] = base;
	resource.clear_value.float32[1] = 1.0f - base;
	resource.clear_value.float32[2] = 0.25f + base * 0.5f;
	resource.clear_value.float32[3] = 1.0f;
	return resource;
}

void submit_and_wait(const vulkan_setup_t& vulkan, VkQueue queue, VkCommandBuffer command_buffer)
{
	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkResult result = vkCreateFence(vulkan.device, &fence_create_info, nullptr, &fence);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
	vkDestroyFence(vulkan.device, fence, nullptr);
}

}

int main(int argc, char** argv)
{
	vulkan_req_t reqs = {};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.required_queue_flags = VK_QUEUE_TRANSFER_BIT;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_linear_optimal_mix", reqs);

	VkFormatProperties format_properties = {};
	vkGetPhysicalDeviceFormatProperties(vulkan.physical, kImageFormat, &format_properties);
	if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0)
	{
		printf("Optimal %s images cannot be cleared on this implementation.\n", "VK_FORMAT_R8G8B8A8_UNORM");
		test_done(vulkan);
		return 77;
	}

	VkPhysicalDeviceMemoryProperties memory_properties = {};
	vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &memory_properties);

	std::vector<buffer_resource_t> buffers;
	std::vector<image_resource_t> images;
	buffers.reserve(kPairCount);
	images.reserve(kPairCount);

	uint32_t common_memory_type_bits = 0xffffffffu;
	for (uint32_t i = 0; i < kPairCount; i++)
	{
		buffers.push_back(create_buffer(vulkan, i));
		images.push_back(create_image(vulkan, i));

		if (buffers.back().memory_info.requires_dedicated || images.back().memory_info.requires_dedicated)
		{
			printf("This implementation requires dedicated allocations for the chosen resources.\n");
			for (auto& image : images) if (image.image) vkDestroyImage(vulkan.device, image.image, nullptr);
			for (auto& buffer : buffers) if (buffer.buffer) vkDestroyBuffer(vulkan.device, buffer.buffer, nullptr);
			test_done(vulkan);
			return 77;
		}

		common_memory_type_bits &= buffers.back().memory_info.requirements.memoryTypeBits;
		common_memory_type_bits &= images.back().memory_info.requirements.memoryTypeBits;
	}

	if (common_memory_type_bits == 0)
	{
		printf("No common Vulkan memory type can hold both the buffers and optimal images.\n");
		for (auto& image : images) vkDestroyImage(vulkan.device, image.image, nullptr);
		for (auto& buffer : buffers) vkDestroyBuffer(vulkan.device, buffer.buffer, nullptr);
		test_done(vulkan);
		return 77;
	}

	const VkDeviceSize granularity = vulkan.device_properties.limits.bufferImageGranularity;
	printf("bufferImageGranularity is %u\n", (unsigned)granularity);
	VkDeviceSize allocation_size = 0;
	bool has_previous = false;
	bool previous_is_linear = false;
	for (uint32_t i = 0; i < kPairCount; i++)
	{
		buffers[i].memory_offset = place_resource(allocation_size, buffers[i].memory_info.requirements, previous_is_linear, true, has_previous, granularity);
		allocation_size = buffers[i].memory_offset + buffers[i].memory_info.requirements.size;
		has_previous = true;
		previous_is_linear = true;

		images[i].memory_offset = place_resource(allocation_size, images[i].memory_info.requirements, previous_is_linear, false, has_previous, granularity);
		allocation_size = images[i].memory_offset + images[i].memory_info.requirements.size;
		previous_is_linear = false;
	}

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = allocation_size;
	allocate_info.memoryTypeIndex = choose_memory_type(memory_properties, common_memory_type_bits);
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &memory);
	check(result);
	assert(memory != VK_NULL_HANDLE);

	for (uint32_t i = 0; i < kPairCount; i++)
	{
		result = vkBindBufferMemory(vulkan.device, buffers[i].buffer, memory, buffers[i].memory_offset);
		check(result);
		result = vkBindImageMemory(vulkan.device, images[i].image, memory, images[i].memory_offset);
		check(result);
	}

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan.queue_family_index;
	result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, nullptr, &command_pool);
	check(result);

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_allocate_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &command_buffer);
	check(result);

	VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
	check(result);

	std::vector<VkImageMemoryBarrier> image_barriers(images.size());
	for (uint32_t i = 0; i < kPairCount; i++)
	{
		image_barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[i].srcAccessMask = 0;
		image_barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[i].image = images[i].image;
		image_barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[i].subresourceRange.baseMipLevel = 0;
		image_barriers[i].subresourceRange.levelCount = 1;
		image_barriers[i].subresourceRange.baseArrayLayer = 0;
		image_barriers[i].subresourceRange.layerCount = 1;
	}
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
	                     image_barriers.size(), image_barriers.data());

	for (uint32_t i = 0; i < kPairCount; i++)
	{
		vkCmdFillBuffer(command_buffer, buffers[i].buffer, 0, kBufferSize, buffers[i].fill_value);
		VkImageSubresourceRange subresource_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdClearColorImage(command_buffer, images[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &images[i].clear_value, 1, &subresource_range);
	}

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	bench_start_iteration(vulkan.bench);
	submit_and_wait(vulkan, queue, command_buffer);

	std::vector<VkBuffer> buffer_handles;
	buffer_handles.reserve(buffers.size());
	for (const auto& buffer : buffers) buffer_handles.push_back(buffer.buffer);
	testQueueBuffer(vulkan, queue, buffer_handles);

	if (vulkan.vkAssertBuffer)
	{
		for (uint32_t i = 0; i < kPairCount; i++)
		{
			uint32_t actual_crc = 0;
			const VkUpdateBufferInfoARM info = { VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffers[i].buffer, 0, VK_WHOLE_SIZE, nullptr };
			result = vulkan.vkAssertBuffer(vulkan.device, &info, &actual_crc, "linear_optimal_mix buffer");
			check(result);
			assert(actual_crc == buffers[i].expected_crc);
		}
	}
	bench_stop_iteration(vulkan.bench);

	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);

	for (auto& image : images) vkDestroyImage(vulkan.device, image.image, nullptr);
	for (auto& buffer : buffers) vkDestroyBuffer(vulkan.device, buffer.buffer, nullptr);
	testFreeMemory(vulkan, memory);
	test_done(vulkan);
	return 0;
}
