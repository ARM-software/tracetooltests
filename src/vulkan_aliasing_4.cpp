#include "vulkan_common.h"

static const uint32_t kWidth = 512;
static const uint32_t kHeight = 512;
static const VkDeviceSize kMip0ReadbackSize = kWidth * kHeight * 4;
static const VkDeviceSize kMip1ReadbackSize = (kWidth / 2) * (kHeight / 2) * 4;
static const VkDeviceSize kMip2ReadbackSize = (kWidth / 4) * (kHeight / 4) * 4;
static const VkDeviceSize kReverseReadbackSize = kMip1ReadbackSize;
static const VkDeviceSize kReadbackSize = kMip0ReadbackSize + kMip1ReadbackSize + kMip2ReadbackSize + kReverseReadbackSize;

struct alias_case_t
{
	uint32_t mip;
	uint32_t layer;
	uint32_t width;
	uint32_t height;
	VkClearColorValue color;
	VkDeviceSize readback_offset;
	bool write_image_2;
};
static const alias_case_t kCases[] = {
	{0, 0, kWidth, kHeight, {{1.0f, 0.0f, 0.0f, 1.0f}}, 0, false},
	{1, 0, kWidth / 2, kHeight / 2, {{0.0f, 1.0f, 0.0f, 1.0f}}, kMip0ReadbackSize, false},
	{2, 1, kWidth / 4, kHeight / 4, {{0.0f, 0.0f, 1.0f, 1.0f}}, kMip0ReadbackSize + kMip1ReadbackSize, false},
	{1, 1, kWidth / 2, kHeight / 2, {{1.0f, 1.0f, 0.0f, 1.0f}}, kMip0ReadbackSize + kMip1ReadbackSize + kMip2ReadbackSize, true},
};

static void assert_pixel(const uint8_t *bytes, VkDeviceSize offset, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	assert(bytes[offset + 0] == r);
	assert(bytes[offset + 1] == g);
	assert(bytes[offset + 2] == b);
	assert(bytes[offset + 3] == a);
}

static void transition_subresource(
	VkCommandBuffer command_buffer,
	VkImage image,
	uint32_t mip,
	uint32_t layer,
	VkImageLayout old_layout,
	VkImageLayout new_layout,
	VkAccessFlags src_access,
	VkAccessFlags dst_access)
{
	VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr};
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = mip;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = layer;
	barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(
		command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier);
}

struct image_memory_info_t
{
	VkMemoryRequirements requirements;
	bool requires_dedicated;
};

static image_memory_info_t get_image_memory_info(const vulkan_setup_t &vulkan, VkImage image)
{
	VkImageMemoryRequirementsInfo2 info = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, nullptr};
	info.image = image;
	VkMemoryDedicatedRequirements dedicated = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS, nullptr};
	VkMemoryRequirements2 requirements = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicated};
	vkGetImageMemoryRequirements2(vulkan.device, &info, &requirements);
	image_memory_info_t out = {};
	out.requirements = requirements.memoryRequirements;
	out.requires_dedicated = dedicated.requiresDedicatedAllocation == VK_TRUE;
	return out;
}

static VkDeviceMemory allocate_memory(const vulkan_setup_t &vulkan, const VkMemoryRequirements &requirements)
{
	VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, 0);

	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &memory);
	check(result);
	assert(memory != VK_NULL_HANDLE);
	return memory;
}

static void createImage(VkDevice device, VkImage *image)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = static_cast<uint32_t>(kWidth);
	imageInfo.extent.height = static_cast<uint32_t>(kHeight);
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 3;
	imageInfo.arrayLayers = 2;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.flags = VK_IMAGE_CREATE_ALIAS_BIT;
	VkResult result = vkCreateImage(device, &imageInfo, nullptr, image);
	check(result);
	assert(*image != VK_NULL_HANDLE);
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_aliasing_4", reqs);

	VkImage im1 = VK_NULL_HANDLE;
	VkImage im2 = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkResult result = VK_SUCCESS;
	createImage(vulkan.device, &im1);
	createImage(vulkan.device, &im2);
	image_memory_info_t info1 = get_image_memory_info(vulkan, im1);
	image_memory_info_t info2 = get_image_memory_info(vulkan, im2);

	if (info1.requires_dedicated || info2.requires_dedicated)
	{
		printf("Skipping: image requires dedicated allocation\n");
		vkDestroyImage(vulkan.device, im2, nullptr);
		vkDestroyImage(vulkan.device, im1, nullptr);
		return 77;
	}
	VkMemoryRequirements req1 = info1.requirements;
	VkMemoryRequirements req2 = info2.requirements;

	VkMemoryRequirements merged = req1;
	merged.memoryTypeBits = req1.memoryTypeBits & req2.memoryTypeBits;
	merged.size = std::max(req1.size, req2.size);
	merged.alignment = std::max(req1.alignment, req2.alignment);
	assert(merged.memoryTypeBits != 0);
	memory = allocate_memory(vulkan, merged);
	result = vkBindImageMemory(vulkan.device, im1, memory, 0);
	check(result);
	result = vkBindImageMemory(vulkan.device, im2, memory, 0);
	check(result);

	VkBuffer readback_buffer = VK_NULL_HANDLE;
	VkDeviceMemory readback_memory = VK_NULL_HANDLE;

	VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	buffer_info.size = kReadbackSize;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &readback_buffer);
	check(result);
	assert(readback_buffer != VK_NULL_HANDLE);

	VkMemoryRequirements readback_requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, readback_buffer, &readback_requirements);

	VkMemoryAllocateInfo readback_allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	readback_allocate_info.allocationSize = readback_requirements.size;
	readback_allocate_info.memoryTypeIndex = get_device_memory_type(
		readback_requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	result = vkAllocateMemory(vulkan.device, &readback_allocate_info, nullptr, &readback_memory);
	check(result);
	assert(readback_memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, readback_buffer, readback_memory, 0);
	check(result);

	VkCommandPool command_pool = VK_NULL_HANDLE;

	VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = vulkan.queue_family_index;

	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
	check(result);

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;

	VkCommandBufferAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	alloc_info.commandPool = command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;

	result = vkAllocateCommandBuffers(vulkan.device, &alloc_info, &command_buffer);
	check(result);
	bench_start_iteration(vulkan.bench);
	VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	for (const alias_case_t &c : kCases)
	{
		assert(c.readback_offset + c.width * c.height * 4 <= kReadbackSize);
		VkImage write_image = c.write_image_2 ? im2 : im1;
		VkImage read_image = c.write_image_2 ? im1 : im2;

		transition_subresource(command_buffer, write_image, c.mip, c.layer,
							   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
							   0, VK_ACCESS_TRANSFER_WRITE_BIT);

		transition_subresource(command_buffer, read_image, c.mip, c.layer,
							   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
							   0, VK_ACCESS_TRANSFER_READ_BIT);

		VkImageSubresourceRange clear_range = {};
		clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		clear_range.baseMipLevel = c.mip;
		clear_range.levelCount = 1;
		clear_range.baseArrayLayer = c.layer;
		clear_range.layerCount = 1;

		vkCmdClearColorImage(command_buffer, write_image, VK_IMAGE_LAYOUT_GENERAL, &c.color, 1, &clear_range);

		VkMemoryBarrier alias_barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr};
		alias_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		alias_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(command_buffer,
							 VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT,
							 0,
							 1,
							 &alias_barrier,
							 0,
							 nullptr,
							 0,
							 nullptr);

		VkBufferImageCopy copy_region = {};
		copy_region.bufferOffset = c.readback_offset;
		copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.imageSubresource.mipLevel = c.mip;
		copy_region.imageSubresource.baseArrayLayer = c.layer;
		copy_region.imageSubresource.layerCount = 1;
		copy_region.imageExtent = {c.width, c.height, 1};

		vkCmdCopyImageToBuffer(command_buffer, read_image, VK_IMAGE_LAYOUT_GENERAL, readback_buffer, 1, &copy_region);
	}

	VkBufferMemoryBarrier readback_barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
	readback_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	readback_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	readback_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	readback_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	readback_barrier.buffer = readback_buffer;
	readback_barrier.offset = 0;
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
	result = vkEndCommandBuffer(command_buffer);
	check(result);
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr};
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);

	VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);

	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
	bench_stop_iteration(vulkan.bench);

	uint8_t *bytes = nullptr;
	result = vkMapMemory(vulkan.device, readback_memory, 0, kReadbackSize, 0, (void **)&bytes);
	check(result);

	assert_pixel(bytes, kCases[0].readback_offset, 255, 0, 0, 255);
	assert_pixel(bytes, kCases[1].readback_offset, 0, 255, 0, 255);
	assert_pixel(bytes, kCases[2].readback_offset, 0, 0, 255, 255);
	assert_pixel(bytes, kCases[3].readback_offset, 255, 255, 0, 255);

	vkUnmapMemory(vulkan.device, readback_memory);
	vkDestroyFence(vulkan.device, fence, nullptr);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyBuffer(vulkan.device, readback_buffer, nullptr);
	testFreeMemory(vulkan, readback_memory);
	vkDestroyImage(vulkan.device, im2, nullptr);
	vkDestroyImage(vulkan.device, im1, nullptr);
	testFreeMemory(vulkan, memory);
	test_done(vulkan);

	return 0;
}
