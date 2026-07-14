#include "vulkan_window_common.h"

static VkCompositeAlphaFlagBitsKHR select_composite_alpha(VkCompositeAlphaFlagsKHR supported)
{
	if (supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
	assert(supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
	return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.surface = true;
	reqs.instance_extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_strict_sync_window", reqs);
	testwindow window = test_window_create(vulkan, 0, 0, 320, 240);

	VkSurfaceCapabilitiesKHR capabilities = {};
	VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan.physical, window.surface, &capabilities);
	check(result);
	uint32_t format_count = 0;
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, window.surface, &format_count, nullptr);
	check(result);
	assert(format_count > 0);
	std::vector<VkSurfaceFormatKHR> formats(format_count);
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, window.surface, &format_count, formats.data());
	check(result);
	uint32_t mode_count = 0;
	result = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, window.surface, &mode_count, nullptr);
	check(result);
	assert(mode_count > 0);
	std::vector<VkPresentModeKHR> modes(mode_count);
	result = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, window.surface, &mode_count, modes.data());
	check(result);

	VkExtent2D extent = capabilities.currentExtent;
	if (extent.width == UINT32_MAX) extent = { 320, 240 };
	const VkImageUsageFlags image_usage = capabilities.supportedUsageFlags & (~capabilities.supportedUsageFlags + 1);
	assert(image_usage != 0);
	VkSwapchainCreateInfoKHR swapchain_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr };
	swapchain_info.surface = window.surface;
	swapchain_info.minImageCount = capabilities.minImageCount;
	swapchain_info.imageFormat = formats[0].format;
	swapchain_info.imageColorSpace = formats[0].colorSpace;
	swapchain_info.imageExtent = extent;
	swapchain_info.imageArrayLayers = 1;
	swapchain_info.imageUsage = image_usage;
	swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_info.preTransform = capabilities.currentTransform;
	swapchain_info.compositeAlpha = select_composite_alpha(capabilities.supportedCompositeAlpha);
	swapchain_info.presentMode = modes[0];
	swapchain_info.clipped = VK_TRUE;

	bench_start_iteration(vulkan.bench);
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	result = vkCreateSwapchainKHR(vulkan.device, &swapchain_info, nullptr, &swapchain);
	check(result);
	uint32_t image_count = 0;
	result = vkGetSwapchainImagesKHR(vulkan.device, swapchain, &image_count, nullptr);
	check(result);
	assert(image_count > 0);
	std::vector<VkImage> images(image_count);
	result = vkGetSwapchainImagesKHR(vulkan.device, swapchain, &image_count, images.data());
	check(result);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
	check(result);
	VkCommandBufferAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	allocate_info.commandPool = command_pool;
	allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &allocate_info, &command_buffer);
	check(result);
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);
	std::vector<VkImageMemoryBarrier> barriers(image_count);
	for (uint32_t i = 0; i < image_count; i++)
	{
		barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barriers[i].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].image = images[i];
		barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barriers[i].subresourceRange.levelCount = 1;
		barriers[i].subresourceRange.layerCount = 1;
	}
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
	                     0, 0, nullptr, 0, nullptr, image_count, barriers.data());
	result = vkEndCommandBuffer(command_buffer);
	check(result);
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	VkSubmitInfo layout_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	layout_submit.commandBufferCount = 1;
	layout_submit.pCommandBuffers = &command_buffer;

	VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	std::vector<VkSemaphore> acquire_semaphores(image_count, VK_NULL_HANDLE);
	std::vector<uint32_t> acquired_images(image_count, UINT32_MAX);
	for (uint32_t i = 0; i < image_count; i++)
	{
		result = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &acquire_semaphores[i]);
		check(result);
		result = vkAcquireNextImageKHR(vulkan.device, swapchain, 1000000000, acquire_semaphores[i], VK_NULL_HANDLE, &acquired_images[i]);
		if (result == VK_TIMEOUT || result == VK_NOT_READY)
		{
			printf("Skipping: presentation engine does not allow every swapchain image to be acquired simultaneously\n");
			for (VkSemaphore semaphore : acquire_semaphores)
			{
				if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(vulkan.device, semaphore, nullptr);
			}
			vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
			vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
			vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
			bench_stop_iteration(vulkan.bench);
			test_window_destroy(vulkan, window);
			test_done(vulkan);
			return 77;
		}
		check(result);
	}
	std::vector<VkPipelineStageFlags> acquire_stages(image_count, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	layout_submit.waitSemaphoreCount = image_count;
	layout_submit.pWaitSemaphores = acquire_semaphores.data();
	layout_submit.pWaitDstStageMask = acquire_stages.data();
	result = vkQueueSubmit(queue, 1, &layout_submit, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(queue);
	check(result);

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence acquire_fence = VK_NULL_HANDLE;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &acquire_fence);
	check(result);
	uint32_t unavailable_image = UINT32_MAX;
	result = vkAcquireNextImageKHR(vulkan.device, swapchain, 0, VK_NULL_HANDLE, acquire_fence, &unavailable_image);
	assert(result == VK_NOT_READY || result == VK_TIMEOUT);

	VkPresentInfoKHR first_present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr };
	first_present.swapchainCount = 1;
	first_present.pSwapchains = &swapchain;
	first_present.pImageIndices = &acquired_images[0];
	result = vkQueuePresentKHR(queue, &first_present);
	check(result);

	uint32_t reacquired_image = UINT32_MAX;
	result = vkAcquireNextImageKHR(vulkan.device, swapchain, 1000000000, VK_NULL_HANDLE, acquire_fence, &reacquired_image);
	check(result);
	assert(reacquired_image == acquired_images[0]);
	result = vkWaitForFences(vulkan.device, 1, &acquire_fence, VK_TRUE, UINT64_MAX);
	check(result);

	acquired_images[0] = reacquired_image;
	VkPresentInfoKHR final_present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr };
	final_present.swapchainCount = 1;
	final_present.pSwapchains = &swapchain;
	for (uint32_t i = 0; i < image_count; i++)
	{
		final_present.pImageIndices = &acquired_images[i];
		result = vkQueuePresentKHR(queue, &final_present);
		check(result);
		final_present.waitSemaphoreCount = 0;
		final_present.pWaitSemaphores = nullptr;
	}
	result = vkQueueWaitIdle(queue);
	check(result);

	vkDestroyFence(vulkan.device, acquire_fence, nullptr);
	for (VkSemaphore semaphore : acquire_semaphores) vkDestroySemaphore(vulkan.device, semaphore, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
	bench_stop_iteration(vulkan.bench);
	test_window_destroy(vulkan, window);
	test_done(vulkan);
	return 0;
}
