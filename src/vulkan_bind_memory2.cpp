#include "vulkan_common.h"

static VkDeviceMemory allocate_memory(const vulkan_setup_t& vulkan, const VkMemoryRequirements& requirements)
{
	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, 0);

	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &memory);
	check(result);
	assert(memory != VK_NULL_HANDLE);
	return memory;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_0;
	reqs.minApiVersion = VK_API_VERSION_1_0;
	reqs.maxApiVersion = VK_API_VERSION_1_0;
	reqs.device_extensions.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_bind_memory2", reqs);
	assert(vulkan.device_extensions.count(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME) == 1);

	MAKEDEVICEPROCADDR(vulkan, vkBindBufferMemory2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkBindImageMemory2KHR);

	bench_start_iteration(vulkan.bench);

	VkBufferCreateInfo buffer_create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_create_info.size = 4096;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer buffer = VK_NULL_HANDLE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_create_info, nullptr, &buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, "bind_memory2_buffer");

	VkMemoryRequirements buffer_requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, buffer, &buffer_requirements);
	VkDeviceMemory buffer_memory = allocate_memory(vulkan, buffer_requirements);

	VkBindBufferMemoryInfoKHR bind_buffer_info = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO_KHR, nullptr };
	bind_buffer_info.buffer = buffer;
	bind_buffer_info.memory = buffer_memory;
	bind_buffer_info.memoryOffset = 0;
	result = pf_vkBindBufferMemory2KHR(vulkan.device, 1, &bind_buffer_info);
	check(result);
	test_marker_mention(vulkan, "Executed vkBindBufferMemory2KHR", VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer);

	VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_create_info.extent = { 16, 16, 1 };
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImage image = VK_NULL_HANDLE;
	result = vkCreateImage(vulkan.device, &image_create_info, nullptr, &image);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)image, "bind_memory2_image");

	VkMemoryRequirements image_requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, image, &image_requirements);
	VkDeviceMemory image_memory = allocate_memory(vulkan, image_requirements);

	VkBindImageMemoryInfoKHR bind_image_info = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO_KHR, nullptr };
	bind_image_info.image = image;
	bind_image_info.memory = image_memory;
	bind_image_info.memoryOffset = 0;
	result = pf_vkBindImageMemory2KHR(vulkan.device, 1, &bind_image_info);
	check(result);
	test_marker_mention(vulkan, "Executed vkBindImageMemory2KHR", VK_OBJECT_TYPE_IMAGE, (uint64_t)image);

	bench_stop_iteration(vulkan.bench);

	vkDestroyImage(vulkan.device, image, nullptr);
	vkFreeMemory(vulkan.device, image_memory, nullptr);
	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	vkFreeMemory(vulkan.device, buffer_memory, nullptr);

	test_done(vulkan);
	return 0;
}
