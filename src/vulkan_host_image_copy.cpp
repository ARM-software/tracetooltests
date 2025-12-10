#include "vulkan_common.h"

#include <vector>

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)i;
	(void)argc;
	(void)argv;
	(void)reqs;
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.device_extensions.push_back("VK_EXT_host_image_copy");
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	VkPhysicalDeviceHostImageCopyFeatures hostCopyFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES, nullptr };
	hostCopyFeatures.hostImageCopy = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&hostCopyFeatures);

	auto vk = test_init(argc, argv, "vulkan_host_image_copy", reqs);

	MAKEDEVICEPROCADDR(vk, vkCopyMemoryToImageEXT);
	MAKEDEVICEPROCADDR(vk, vkCopyImageToMemoryEXT);
	MAKEDEVICEPROCADDR(vk, vkTransitionImageLayoutEXT);
	MAKEDEVICEPROCADDR(vk, vkCopyImageToImageEXT);
	MAKEDEVICEPROCADDR(vk, vkGetImageSubresourceLayout2EXT);

	bench_start_iteration(vk.bench);

	const uint32_t width = 4;
	const uint32_t height = 4;

	VkImage image = VK_NULL_HANDLE;
	VkImage imageCopy = VK_NULL_HANDLE;
	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent = { width, height, 1 };
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateImage(vk.device, &imageInfo, nullptr, &image);
	check(result);

	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(vk.device, image, &memReq);
	VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = get_device_memory_type(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vk.device, &allocInfo, nullptr, &memory);
	check(result);
	result = vkBindImageMemory(vk.device, image, memory, 0);
	check(result);

	result = vkCreateImage(vk.device, &imageInfo, nullptr, &imageCopy);
	check(result);

	VkMemoryRequirements memReqCopy{};
	vkGetImageMemoryRequirements(vk.device, imageCopy, &memReqCopy);
	VkMemoryAllocateInfo allocInfoCopy{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocInfoCopy.allocationSize = memReqCopy.size;
	allocInfoCopy.memoryTypeIndex = get_device_memory_type(memReqCopy.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkDeviceMemory memoryCopy = VK_NULL_HANDLE;
	result = vkAllocateMemory(vk.device, &allocInfoCopy, nullptr, &memoryCopy);
	check(result);
	result = vkBindImageMemory(vk.device, imageCopy, memoryCopy, 0);
	check(result);

	VkHostImageLayoutTransitionInfo transition{ VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO, nullptr };
	transition.image = image;
	transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	transition.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	transition.subresourceRange.baseMipLevel = 0;
	transition.subresourceRange.levelCount = 1;
	transition.subresourceRange.baseArrayLayer = 0;
	transition.subresourceRange.layerCount = 1;
	result = pf_vkTransitionImageLayoutEXT(vk.device, 1, &transition);
	check(result);

	VkHostImageLayoutTransitionInfo transitionCopy{ VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO, nullptr };
	transitionCopy.image = imageCopy;
	transitionCopy.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	transitionCopy.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	transitionCopy.subresourceRange = transition.subresourceRange;
	result = pf_vkTransitionImageLayoutEXT(vk.device, 1, &transitionCopy);
	check(result);

	std::vector<uint8_t> data(width * height * 4);
	for (uint32_t i = 0; i < width * height; ++i)
	{
		data[i * 4 + 0] = static_cast<uint8_t>(i);
		data[i * 4 + 1] = static_cast<uint8_t>(255 - i);
		data[i * 4 + 2] = 42;
		data[i * 4 + 3] = 255;
	}

	VkMemoryToImageCopy regionToImage{ VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY, nullptr };
	regionToImage.pHostPointer = data.data();
	regionToImage.memoryRowLength = 0;
	regionToImage.memoryImageHeight = 0;
	regionToImage.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	regionToImage.imageSubresource.mipLevel = 0;
	regionToImage.imageSubresource.baseArrayLayer = 0;
	regionToImage.imageSubresource.layerCount = 1;
	regionToImage.imageOffset = { 0, 0, 0 };
	regionToImage.imageExtent = { width, height, 1 };

	VkImageSubresource2 subresourceInfo{ VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2, nullptr };
	subresourceInfo.imageSubresource.aspectMask = regionToImage.imageSubresource.aspectMask;
	subresourceInfo.imageSubresource.mipLevel = regionToImage.imageSubresource.mipLevel;
	subresourceInfo.imageSubresource.arrayLayer = regionToImage.imageSubresource.baseArrayLayer;
	VkSubresourceLayout2 subresourceLayout{ VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2, nullptr };
	pf_vkGetImageSubresourceLayout2EXT(vk.device, image, &subresourceInfo, &subresourceLayout);

	VkSubresourceLayout2 subresourceLayoutCopy{ VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2, nullptr };
	pf_vkGetImageSubresourceLayout2EXT(vk.device, imageCopy, &subresourceInfo, &subresourceLayoutCopy);

	VkCopyMemoryToImageInfo copyToImage{ VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO, nullptr };
	copyToImage.flags = 0;
	copyToImage.dstImage = image;
	copyToImage.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
	copyToImage.regionCount = 1;
	copyToImage.pRegions = &regionToImage;
	result = pf_vkCopyMemoryToImageEXT(vk.device, &copyToImage);
	check(result);

	std::vector<uint8_t> readback(data.size(), 0);
	VkImageToMemoryCopy regionToMemory{ VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY, nullptr };
	regionToMemory.pHostPointer = readback.data();
	regionToMemory.memoryRowLength = 0;
	regionToMemory.memoryImageHeight = 0;
	regionToMemory.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	regionToMemory.imageSubresource.mipLevel = 0;
	regionToMemory.imageSubresource.baseArrayLayer = 0;
	regionToMemory.imageSubresource.layerCount = 1;
	regionToMemory.imageOffset = { 0, 0, 0 };
	regionToMemory.imageExtent = { width, height, 1 };

	VkImageCopy2 imageCopyRegion{ VK_STRUCTURE_TYPE_IMAGE_COPY_2, nullptr };
	imageCopyRegion.srcSubresource = regionToImage.imageSubresource;
	imageCopyRegion.dstSubresource = regionToImage.imageSubresource;
	imageCopyRegion.extent = regionToImage.imageExtent;

	VkCopyImageToImageInfo copyImageToImage{ VK_STRUCTURE_TYPE_COPY_IMAGE_TO_IMAGE_INFO, nullptr };
	copyImageToImage.flags = 0;
	copyImageToImage.srcImage = image;
	copyImageToImage.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
	copyImageToImage.dstImage = imageCopy;
	copyImageToImage.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
	copyImageToImage.regionCount = 1;
	copyImageToImage.pRegions = &imageCopyRegion;
	result = pf_vkCopyImageToImageEXT(vk.device, &copyImageToImage);
	check(result);

	VkCopyImageToMemoryInfo copyToMemory{ VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO, nullptr };
	copyToMemory.flags = 0;
	copyToMemory.srcImage = imageCopy;
	copyToMemory.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
	copyToMemory.regionCount = 1;
	copyToMemory.pRegions = &regionToMemory;
	result = pf_vkCopyImageToMemoryEXT(vk.device, &copyToMemory);
	check(result);

	assert(readback == data);

	bench_stop_iteration(vk.bench);

	vkDestroyImage(vk.device, image, nullptr);
	vkDestroyImage(vk.device, imageCopy, nullptr);
	testFreeMemory(vk, memory);
	testFreeMemory(vk, memoryCopy);

	test_done(vk);
	return 0;
}
