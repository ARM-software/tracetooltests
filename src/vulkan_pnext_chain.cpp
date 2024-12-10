// Unit test to try out various combinations of GPU memory copying and synchronization with CPU-side
// memory mapping.

#include "vulkan_common.h"

static vulkan_req_t reqs;

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

int main(int argc, char** argv)
{
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.instance_extensions = { "VK_EXT_debug_utils" };
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_pnext_chain", reqs);
	VkResult result;

	// Create everything to submit debug labels to mark end of frames

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	VkCommandPoolCreateInfo commandPoolCreateInfo;
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.pNext = nullptr;
	commandPoolCreateInfo.queueFamilyIndex = 0;
	commandPoolCreateInfo.flags = 0;

	VkCommandPool commandPool;
	result = vkCreateCommandPool(vulkan.device, &commandPoolCreateInfo, nullptr, &commandPool);
	assert(result == VK_SUCCESS);

	VkCommandBufferAllocateInfo commandBufferAllocateInfo;
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.pNext = nullptr;
	commandBufferAllocateInfo.commandPool = commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	result = vkAllocateCommandBuffers(vulkan.device, &commandBufferAllocateInfo, &commandBuffer);
	assert(result == VK_SUCCESS);

	VkCommandBufferBeginInfo commandBufferBeginInfo;
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBufferBeginInfo.pNext = nullptr;
	commandBufferBeginInfo.flags = 0;
	commandBufferBeginInfo.pInheritanceInfo = nullptr;

	result = vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);
	assert(result == VK_SUCCESS);

	VkDebugUtilsLabelEXT debugLabel;
	debugLabel.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
	debugLabel.pNext = nullptr;
	debugLabel.pLabelName = "vr-marker,frame_end,type,application";
	debugLabel.color[0] = 1.f;
	debugLabel.color[1] = 0.f;
	debugLabel.color[2] = 0.f;
	debugLabel.color[3] = 1.f;

	vulkan.vkCmdInsertDebugUtilsLabel(commandBuffer, &debugLabel);

	result = vkEndCommandBuffer(commandBuffer);
	assert(result == VK_SUCCESS);

	VkFenceCreateInfo fenceCreateInfo;
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = 0;

	VkFence fence;
	result = vkCreateFence(vulkan.device, &fenceCreateInfo, nullptr, &fence);
	assert(result == VK_SUCCESS);

	VkSubmitInfo submitInfo;
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = nullptr;
	submitInfo.pWaitDstStageMask = nullptr;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = nullptr;

	// Create image and associated memory

	constexpr uint32_t queueFamilyIndex = 0;
	VkImageCreateInfo imageCreateInfo;
	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.pNext = nullptr;
	imageCreateInfo.flags = 0;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCreateInfo.extent.width = 192;
	imageCreateInfo.extent.height = 108;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 1;
	imageCreateInfo.pQueueFamilyIndices = &queueFamilyIndex;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImage image;
	result = vkCreateImage(vulkan.device, &imageCreateInfo, nullptr, &image);
	assert(result == VK_SUCCESS);

	VkMemoryRequirements memoryRequirements;
	vkGetImageMemoryRequirements(vulkan.device, image, &memoryRequirements);

	VkMemoryAllocateInfo memoryAllocateInfo;
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = nullptr;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = 0;

	VkDeviceMemory deviceMemory;
	result = vkAllocateMemory(vulkan.device, &memoryAllocateInfo, nullptr, &deviceMemory);
	assert(result == VK_SUCCESS);

	/* Where the magic happens...

	Those lines check that the trace tool does not save and reuse application owned pointers
	after the vulkan call. So we allocate and free a pointer before/after EACH call. If the
	trace tool tries to re-use it, hopefully, it will trigger a SEGFAULT. */

	VkBindImageMemoryDeviceGroupInfo deviceGroupInfo;
	deviceGroupInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO;
	deviceGroupInfo.pNext = nullptr;
	deviceGroupInfo.deviceIndexCount = 1;
	deviceGroupInfo.pDeviceIndices = nullptr;
	deviceGroupInfo.splitInstanceBindRegionCount = 0;
	deviceGroupInfo.pSplitInstanceBindRegions = nullptr;

	VkBindImageMemoryInfo bindInfo;
	bindInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
	bindInfo.pNext = &deviceGroupInfo;
	bindInfo.image = image;
	bindInfo.memory = deviceMemory;
	bindInfo.memoryOffset = 0;

	for (uint32_t i = 0; i < 10000; ++i)
	{
		result = vkResetFences(vulkan.device, 1, &fence);
		assert(result == VK_SUCCESS);

		result = vkQueueSubmit(queue, 1, &submitInfo, fence);
		assert(result == VK_SUCCESS);

		result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
		assert(result == VK_SUCCESS);

		deviceGroupInfo.pDeviceIndices = new uint32_t(0);

		result = vkBindImageMemory2(vulkan.device, 1, &bindInfo);
		assert(result == VK_SUCCESS);

		delete deviceGroupInfo.pDeviceIndices;

		// recreate the image so that we can rebind on it
		vkDestroyImage(vulkan.device, image, nullptr);
		result = vkCreateImage(vulkan.device, &imageCreateInfo, nullptr, &image);
		assert(result == VK_SUCCESS);
		bindInfo.image = image;
	}

	// Free/Destroy everything

	vkDestroyImage(vulkan.device, image, nullptr);
	vkFreeMemory(vulkan.device, deviceMemory, nullptr);
	vkDestroyFence(vulkan.device, fence, nullptr);
	vkDestroyCommandPool(vulkan.device, commandPool, nullptr);
	test_done(vulkan);

	return 0;
}
