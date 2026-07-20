#include "vulkan_common.h"

static const uint32_t kWidth = 100;
static const uint32_t kHeight = 100;
static const VkDeviceSize kReadbackSize = kWidth * kHeight * 4;

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

void createImage(VkDevice device, VkImage *image)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	// todo: remove placeholder width
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
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_image_aliasing", reqs);

	// todo: fill this out
	VkImage im1 = VK_NULL_HANDLE;
	VkImage im2 = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	createImage(vulkan.device, &im1);
	createImage(vulkan.device, &im2);
	VkMemoryRequirements req1;
	VkMemoryRequirements req2;
	vkGetImageMemoryRequirements(vulkan.device, im1, &req1);
	vkGetImageMemoryRequirements(vulkan.device, im2, &req2);

	VkMemoryRequirements merged = req1;
	merged.memoryTypeBits = req1.memoryTypeBits & req2.memoryTypeBits;
	merged.size = std::max(req1.size, req2.size);
	merged.alignment = std::max(req1.alignment, req2.alignment);
	assert(merged.memoryTypeBits != 0);
	memory = allocate_memory(vulkan, merged);
	check(vkBindImageMemory(vulkan.device, im1, memory, 0));
	check(vkBindImageMemory(vulkan.device, im2, memory, 0));

	VkBuffer readback_buffer = VK_NULL_HANDLE;
	VkDeviceMemory readback_memory = VK_NULL_HANDLE;

	VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	buffer_info.size = kReadbackSize;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &readback_buffer);
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

	VkImageMemoryBarrier barriers[2] = {};
	for (uint32_t i = 0; i < 2; i++)
	{
		barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barriers[i].srcAccessMask = 0;
		barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
		barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].image = i == 0 ? im1 : im2;
		barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barriers[i].subresourceRange.levelCount = 1;
		barriers[i].subresourceRange.layerCount = 1;
	}

	vkCmdPipelineBarrier(command_buffer,
						 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
						 VK_PIPELINE_STAGE_TRANSFER_BIT,
						 0,
						 0,
						 nullptr,
						 0,
						 nullptr,
						 2,
						 barriers);

	VkClearColorValue clear_color = {};
	clear_color.float32[0] = 1.0f;
	clear_color.float32[1] = 0.0f;
	clear_color.float32[2] = 0.0f;
	clear_color.float32[3] = 1.0f;

	VkImageSubresourceRange clear_range = {};
	clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	clear_range.levelCount = 1;
	clear_range.layerCount = 1;

	vkCmdClearColorImage(command_buffer, im1, VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1, &clear_range);

	VkMemoryBarrier alias_barrier = {};
	alias_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
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
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent = {100, 100, 1};

	vkCmdCopyImageToBuffer(command_buffer, im2, VK_IMAGE_LAYOUT_GENERAL, readback_buffer, 1, &copy_region);

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

	assert(bytes[0] == 255);
	assert(bytes[1] == 0);
	assert(bytes[2] == 0);
	assert(bytes[3] == 255);

	vkUnmapMemory(vulkan.device, readback_memory);
	vkDestroyFence(vulkan.device, fence, nullptr);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyBuffer(vulkan.device, readback_buffer, nullptr);
	vkFreeMemory(vulkan.device, readback_memory, nullptr);
	vkDestroyImage(vulkan.device, im2, nullptr);
	vkDestroyImage(vulkan.device, im1, nullptr);
	vkFreeMemory(vulkan.device, memory, nullptr);
	test_done(vulkan);

	return 0;
}
