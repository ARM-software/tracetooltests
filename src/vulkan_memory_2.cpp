// Test intermixing linear and opaque content in the same buffer

#include "vulkan_common.h"

static int offset = 0;
static bool optimal_image = false;

static void show_usage()
{
	printf("-O / --offset N       Add an offset to the buffer (default %d)\n", offset);
	printf("-I / --optimal-image  Request an optimal image instead of linear (only works on some GPUs)\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-O", "--offset"))
	{
		offset = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-I", "--optimal-image"))
	{
		optimal_image = true;
		return true;
	}
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_1;
	std::string testname = "vulkan_memory_2";
	vulkan_setup_t vulkan = test_init(argc, argv, testname, reqs);
	VkQueue queue;

	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	VkCommandPool cmdpool;
	VkCommandPoolCreateInfo cmdcreateinfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	cmdcreateinfo.flags = 0;
	cmdcreateinfo.queueFamilyIndex = 0;
	VkResult result = vkCreateCommandPool(vulkan.device, &cmdcreateinfo, nullptr, &cmdpool);
	check(result);

	std::vector<VkCommandBuffer> cmdbuffers(10);
	VkCommandBufferAllocateInfo pAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	pAllocateInfo.commandBufferCount = 10;
	pAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	pAllocateInfo.commandPool = cmdpool;
	pAllocateInfo.pNext = nullptr;
	result = vkAllocateCommandBuffers(vulkan.device, &pAllocateInfo, cmdbuffers.data());
	check(result);

	VkBuffer buffer;
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = 1024 * 1024;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer);
	check(result);

	VkMemoryRequirements buffer_req;
	vkGetBufferMemoryRequirements(vulkan.device, buffer, &buffer_req);
	const uint32_t bufferMemoryTypeIndex = get_device_memory_type(buffer_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	constexpr uint32_t queueFamilyIndex = 0;
	VkImageCreateInfo imageCreateInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	imageCreateInfo.flags = 0;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCreateInfo.extent.width = 192;
	imageCreateInfo.extent.height = 108;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	if (optimal_image) imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	else imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 1;
	imageCreateInfo.pQueueFamilyIndices = &queueFamilyIndex;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImage image;
	result = vkCreateImage(vulkan.device, &imageCreateInfo, nullptr, &image);
	assert(result == VK_SUCCESS);

	VkMemoryRequirements image_req;
	vkGetImageMemoryRequirements(vulkan.device, image, &image_req);
	const uint32_t imageMemoryTypeIndex = get_device_memory_type(image_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	if (imageMemoryTypeIndex != bufferMemoryTypeIndex)
	{
		printf("We could not place an opaque image into our host-side buffer\n"); // this is an issue eg on nvidia - use llvmpipe instead
		exit(77);
	}

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = bufferMemoryTypeIndex;
	pAllocateMemInfo.allocationSize = offset;
	const unsigned buffer_start = aligned_size(pAllocateMemInfo.allocationSize, buffer_req.alignment);
	pAllocateMemInfo.allocationSize = buffer_start + buffer_req.size;
	const unsigned image_start = aligned_size(pAllocateMemInfo.allocationSize, image_req.alignment);
	pAllocateMemInfo.allocationSize = image_start + image_req.size;
	ILOG("Allocating %lu (%lu more due to alignment) memory from offset=%d buffer=start=%u,size=%lu,alignment %u image=start=%u,size=%lu,alignment %u", (unsigned long)pAllocateMemInfo.allocationSize,
		(pAllocateMemInfo.allocationSize - buffer_req.size - image_req.size - offset), offset, buffer_start, (unsigned long)buffer_req.size, (unsigned)buffer_req.alignment, image_start, (unsigned long)image_req.size, (unsigned)image_req.alignment);
	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	assert(memory != 0);
	check(result);

	vkBindImageMemory(vulkan.device, image, memory, image_start);
	vkBindBufferMemory(vulkan.device, buffer, memory, buffer_start);

	// Do some dummy workload here
	testQueueBuffer(vulkan, queue, { buffer });

	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	vkDestroyImage(vulkan.device, image, nullptr);

	testFreeMemory(vulkan, memory);
	vkFreeCommandBuffers(vulkan.device, cmdpool, cmdbuffers.size(), cmdbuffers.data());
	vkDestroyCommandPool(vulkan.device, cmdpool, nullptr);

	test_done(vulkan);

	return 0;
}
