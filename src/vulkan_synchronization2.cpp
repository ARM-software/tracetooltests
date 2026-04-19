#include "vulkan_common.h"

#include <array>
#include <cstring>
#include <vector>

namespace
{

constexpr VkDeviceSize kBufferSize = 256;
constexpr VkFormat kImageFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct BufferResource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct ImageResource
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

BufferResource create_buffer(const vulkan_setup_t& vulkan, VkBufferUsageFlags usage, const char* name)
{
	BufferResource resource;

	VkBufferCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	create_info.size = kBufferSize;
	create_info.usage = usage;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &create_info, nullptr, &resource.buffer);
	check(result);
	assert(resource.buffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resource.buffer, name);

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, resource.buffer, &requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(
		requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, resource.buffer, resource.memory, 0);
	check(result);
	return resource;
}

ImageResource create_image(const vulkan_setup_t& vulkan, const char* name)
{
	ImageResource resource;

	VkImageCreateInfo create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	create_info.imageType = VK_IMAGE_TYPE_2D;
	create_info.format = kImageFormat;
	create_info.extent = { 1, 1, 1 };
	create_info.mipLevels = 1;
	create_info.arrayLayers = 1;
	create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage(vulkan.device, &create_info, nullptr, &resource.image);
	check(result);
	assert(resource.image != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)resource.image, name);

	VkMemoryRequirements requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, resource.image, &requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, 0);

	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);

	result = vkBindImageMemory(vulkan.device, resource.image, resource.memory, 0);
	check(result);
	return resource;
}

void write_buffer(const vulkan_setup_t& vulkan, const BufferResource& resource, const std::vector<uint8_t>& data)
{
	assert(data.size() == kBufferSize);
	uint8_t* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, resource.memory, 0, kBufferSize, 0, (void**)&mapped);
	check(result);
	assert(mapped != nullptr);
	std::memcpy(mapped, data.data(), data.size());
	vkUnmapMemory(vulkan.device, resource.memory);
}

std::vector<uint8_t> read_buffer(const vulkan_setup_t& vulkan, const BufferResource& resource)
{
	uint8_t* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, resource.memory, 0, kBufferSize, 0, (void**)&mapped);
	check(result);
	assert(mapped != nullptr);
	std::vector<uint8_t> data(mapped, mapped + kBufferSize);
	vkUnmapMemory(vulkan.device, resource.memory);
	return data;
}

void destroy_buffer(const vulkan_setup_t& vulkan, BufferResource& resource)
{
	if (resource.buffer) vkDestroyBuffer(vulkan.device, resource.buffer, nullptr);
	if (resource.memory) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

void destroy_image(const vulkan_setup_t& vulkan, ImageResource& resource)
{
	if (resource.image) vkDestroyImage(vulkan.device, resource.image, nullptr);
	if (resource.memory) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

std::vector<uint8_t> make_pattern()
{
	std::vector<uint8_t> data(kBufferSize);
	for (uint32_t i = 0; i < kBufferSize; i++) data[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xffu);
	return data;
}

}

int main(int argc, char** argv)
{
	VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, nullptr, VK_TRUE
	};

	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.maxApiVersion = VK_API_VERSION_1_2;
	reqs.device_extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&synchronization2);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_synchronization2", reqs);
	assert(vulkan.device_extensions.count(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 1);
	assert(vulkan.hasfeat13.synchronization2 == VK_TRUE);

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan.physical, &queue_family_count, nullptr);
	assert(queue_family_count > 0);
	std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan.physical, &queue_family_count, queue_family_properties.data());
	if (queue_family_properties[0].timestampValidBits == 0)
	{
		printf("Queue family 0 does not support timestamp queries.\n");
		test_done(vulkan);
		return 77;
	}

	MAKEDEVICEPROCADDR(vulkan, vkCmdSetEvent2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdResetEvent2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdWaitEvents2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdPipelineBarrier2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdWriteTimestamp2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkQueueSubmit2KHR);

	BufferResource upload = create_buffer(vulkan, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "sync2_upload");
	BufferResource intermediate = create_buffer(vulkan, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "sync2_intermediate");
	BufferResource download = create_buffer(vulkan, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "sync2_download");
	ImageResource image = create_image(vulkan, "sync2_image");

	std::vector<uint8_t> expected = make_pattern();
	std::vector<uint8_t> zeros(kBufferSize, 0);
	write_buffer(vulkan, upload, expected);
	write_buffer(vulkan, intermediate, zeros);
	write_buffer(vulkan, download, zeros);

	VkEventCreateInfo event_info = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, nullptr };
	VkEvent event = VK_NULL_HANDLE;
	VkResult result = vkCreateEvent(vulkan.device, &event_info, nullptr, &event);
	check(result);
	assert(event != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_EVENT, (uint64_t)event, "sync2_event");

	VkQueryPoolCreateInfo query_pool_info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr };
	query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	query_pool_info.queryCount = 2;
	VkQueryPool query_pool = VK_NULL_HANDLE;
	result = vkCreateQueryPool(vulkan.device, &query_pool_info, nullptr, &query_pool);
	check(result);
	assert(query_pool != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_QUERY_POOL, (uint64_t)query_pool, "sync2_query_pool");

	VkCommandPoolCreateInfo command_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_info.queueFamilyIndex = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &command_pool_info, nullptr, &command_pool);
	check(result);
	assert(command_pool != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)command_pool, "sync2_command_pool");

	std::array<VkCommandBuffer, 2> command_buffers{};
	VkCommandBufferAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	allocate_info.commandPool = command_pool;
	allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate_info.commandBufferCount = command_buffers.size();
	result = vkAllocateCommandBuffers(vulkan.device, &allocate_info, command_buffers.data());
	check(result);
	for (size_t i = 0; i < command_buffers.size(); i++)
	{
		assert(command_buffers[i] != VK_NULL_HANDLE);
		test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffers[i], i == 0 ? "sync2_command_buffer_a" : "sync2_command_buffer_b");
	}

	VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	VkSemaphore semaphore = VK_NULL_HANDLE;
	result = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &semaphore);
	check(result);
	assert(semaphore != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)semaphore, "sync2_semaphore");

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fence = VK_NULL_HANDLE;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);
	assert(fence != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_FENCE, (uint64_t)fence, "sync2_fence");

	test_marker_mention(vulkan, "record synchronization2 workload", VK_OBJECT_TYPE_EVENT, (uint64_t)event);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffers[0], &begin_info);
	check(result);

	vkCmdResetQueryPool(command_buffers[0], query_pool, 0, 2);

	VkBufferCopy copy_region = { 0, 0, kBufferSize };
	vkCmdCopyBuffer(command_buffers[0], upload.buffer, intermediate.buffer, 1, &copy_region);

	VkBufferMemoryBarrier2KHR buffer_barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr };
	buffer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	buffer_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	buffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	buffer_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrier.buffer = intermediate.buffer;
	buffer_barrier.offset = 0;
	buffer_barrier.size = VK_WHOLE_SIZE;

	VkDependencyInfoKHR event_dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR, nullptr };
	event_dependency.bufferMemoryBarrierCount = 1;
	event_dependency.pBufferMemoryBarriers = &buffer_barrier;

	pf_vkCmdSetEvent2KHR(command_buffers[0], event, &event_dependency);
	VkEvent events[] = { event };
	pf_vkCmdWaitEvents2KHR(command_buffers[0], 1, events, &event_dependency);

	vkCmdCopyBuffer(command_buffers[0], intermediate.buffer, download.buffer, 1, &copy_region);
	pf_vkCmdResetEvent2KHR(command_buffers[0], event, VK_PIPELINE_STAGE_2_COPY_BIT);

	VkMemoryBarrier2KHR memory_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR, nullptr };
	memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	memory_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

	VkImageMemoryBarrier2KHR image_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR, nullptr };
	image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	image_barrier.srcAccessMask = VK_ACCESS_2_NONE;
	image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.image = image.image;
	image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.subresourceRange.baseMipLevel = 0;
	image_barrier.subresourceRange.levelCount = 1;
	image_barrier.subresourceRange.baseArrayLayer = 0;
	image_barrier.subresourceRange.layerCount = 1;

	VkDependencyInfoKHR barrier_dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR, nullptr };
	barrier_dependency.memoryBarrierCount = 1;
	barrier_dependency.pMemoryBarriers = &memory_barrier;
	barrier_dependency.imageMemoryBarrierCount = 1;
	barrier_dependency.pImageMemoryBarriers = &image_barrier;

	pf_vkCmdPipelineBarrier2KHR(command_buffers[0], &barrier_dependency);
	pf_vkCmdWriteTimestamp2KHR(command_buffers[0], VK_PIPELINE_STAGE_2_COPY_BIT, query_pool, 0);

	result = vkEndCommandBuffer(command_buffers[0]);
	check(result);

	result = vkBeginCommandBuffer(command_buffers[1], &begin_info);
	check(result);
	pf_vkCmdWriteTimestamp2KHR(command_buffers[1], VK_PIPELINE_STAGE_2_COPY_BIT, query_pool, 1);
	result = vkEndCommandBuffer(command_buffers[1]);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	VkCommandBufferSubmitInfoKHR submit_command_a = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR, nullptr };
	submit_command_a.commandBuffer = command_buffers[0];
	submit_command_a.deviceMask = 0;

	VkSemaphoreSubmitInfoKHR signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr };
	signal_info.semaphore = semaphore;
	signal_info.value = 0;
	signal_info.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	signal_info.deviceIndex = 0;

	VkSubmitInfo2KHR submit_a = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR, nullptr };
	submit_a.commandBufferInfoCount = 1;
	submit_a.pCommandBufferInfos = &submit_command_a;
	submit_a.signalSemaphoreInfoCount = 1;
	submit_a.pSignalSemaphoreInfos = &signal_info;

	VkCommandBufferSubmitInfoKHR submit_command_b = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR, nullptr };
	submit_command_b.commandBuffer = command_buffers[1];
	submit_command_b.deviceMask = 0;

	VkSemaphoreSubmitInfoKHR wait_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr };
	wait_info.semaphore = semaphore;
	wait_info.value = 0;
	wait_info.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	wait_info.deviceIndex = 0;

	VkSubmitInfo2KHR submit_b = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR, nullptr };
	submit_b.waitSemaphoreInfoCount = 1;
	submit_b.pWaitSemaphoreInfos = &wait_info;
	submit_b.commandBufferInfoCount = 1;
	submit_b.pCommandBufferInfos = &submit_command_b;

	test_marker_mention(vulkan, "submit synchronization2 workload", VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)semaphore);
	bench_start_iteration(vulkan.bench);
	result = pf_vkQueueSubmit2KHR(queue, 1, &submit_a, VK_NULL_HANDLE);
	check(result);
	result = pf_vkQueueSubmit2KHR(queue, 1, &submit_b, fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
	bench_stop_iteration(vulkan.bench);

	std::array<uint64_t, 2> timestamps = {};
	result = vkGetQueryPoolResults(vulkan.device, query_pool, 0, timestamps.size(), sizeof(timestamps), timestamps.data(), sizeof(uint64_t),
	                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
	check(result);

	test_marker_mention(vulkan, "verify synchronization2 copies", VK_OBJECT_TYPE_BUFFER, (uint64_t)download.buffer);
	const std::vector<uint8_t> actual = read_buffer(vulkan, download);
	assert(actual == expected);

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkDestroySemaphore(vulkan.device, semaphore, nullptr);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyQueryPool(vulkan.device, query_pool, nullptr);
	vkDestroyEvent(vulkan.device, event, nullptr);
	destroy_image(vulkan, image);
	destroy_buffer(vulkan, download);
	destroy_buffer(vulkan, intermediate);
	destroy_buffer(vulkan, upload);
	test_done(vulkan);
	return 0;
}
