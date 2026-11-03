#include "vulkan_common.h"
#include "vulkan_window_common.h"

static VkCompositeAlphaFlagBitsKHR pick_composite_alpha(VkCompositeAlphaFlagsKHR supported)
{
	assert(supported != 0);
	if (supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
		return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
		return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
		return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
		return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	assert(!"No supported composite alpha mode");
	return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static bool present_wait2_supported(VkPhysicalDevice physical_device)
{
	VkPhysicalDevicePresentWait2FeaturesKHR present_wait2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR, nullptr};
	VkPhysicalDevicePresentId2FeaturesKHR present_id2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR, &present_wait2};
	VkPhysicalDeviceFeatures2 device_feature = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &present_id2};
	vkGetPhysicalDeviceFeatures2(physical_device, &device_feature);
	return present_id2.presentId2 && present_wait2.presentWait2;
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	reqs.physical_device_supported = present_wait2_supported;
	reqs.surface = true;
	const char *winsys = getenv("TOOLSTEST_WINSYS");
	if (winsys && strcmp(winsys, "headless") == 0)
	{
		reqs.instance_extensions.push_back("VK_EXT_headless_surface");
	}
	else
	{
		reqs.instance_extensions.push_back("VK_KHR_xcb_surface");
	}
	reqs.instance_extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME);
	VkPhysicalDevicePresentWait2FeaturesKHR present_wait2_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR,
	                                                                  nullptr, VK_TRUE};
	VkPhysicalDevicePresentId2FeaturesKHR present_id2_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR,
	                                                           &present_wait2_features, VK_TRUE};
	reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&present_id2_features);
	reqs.minApiVersion = VK_API_VERSION_1_1;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_present_wait2", reqs);

	testwindow window = test_window_create(vulkan, 0, 0, 400, 300);
	VkBool32 present_support = VK_FALSE;
	check(vkGetPhysicalDeviceSurfaceSupportKHR(vulkan.physical, vulkan.queue_family_index, window.surface, &present_support));
	if (!present_support)
	{
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}

	VkSurfaceCapabilitiesPresentWait2KHR wait2_caps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR};
	VkSurfaceCapabilitiesPresentId2KHR id2_caps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR, &wait2_caps};
	VkSurfaceCapabilities2KHR surface_caps2{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR, &id2_caps};
	VkPhysicalDeviceSurfaceInfo2KHR surface_info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR, nullptr, window.surface};

	check(vkGetPhysicalDeviceSurfaceCapabilities2KHR(vulkan.physical, &surface_info, &surface_caps2));

	if (!id2_caps.presentId2Supported || !wait2_caps.presentWait2Supported)
	{
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}

	uint32_t format_count = 0;
	check(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, window.surface, &format_count, nullptr));
	assert(format_count > 0);

	std::vector<VkSurfaceFormatKHR> formats(format_count);
	check(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, window.surface, &format_count, formats.data()));

	VkSurfaceFormatKHR surface_format = formats[0];

	if (format_count == 1 && surface_format.format == VK_FORMAT_UNDEFINED)
	{
		surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
	}

	VkSwapchainCreateInfoKHR swapchain_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr};
	swapchain_info.surface = window.surface;
	swapchain_info.flags = VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR;
	swapchain_info.minImageCount = surface_caps2.surfaceCapabilities.minImageCount + 1;
	if (surface_caps2.surfaceCapabilities.maxImageCount != 0)
	{
		swapchain_info.minImageCount = std::min(swapchain_info.minImageCount, surface_caps2.surfaceCapabilities.maxImageCount);
	}
	swapchain_info.imageFormat = surface_format.format;
	swapchain_info.imageColorSpace = surface_format.colorSpace;
	swapchain_info.imageExtent = surface_caps2.surfaceCapabilities.currentExtent;
	if (swapchain_info.imageExtent.width == UINT32_MAX)
	{
		swapchain_info.imageExtent.width = std::max(surface_caps2.surfaceCapabilities.minImageExtent.width,
		                                            std::min(400u, surface_caps2.surfaceCapabilities.maxImageExtent.width));
		swapchain_info.imageExtent.height = std::max(surface_caps2.surfaceCapabilities.minImageExtent.height,
		                                             std::min(300u, surface_caps2.surfaceCapabilities.maxImageExtent.height));
	}
	swapchain_info.imageArrayLayers = 1;

	assert(surface_caps2.surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

	swapchain_info.preTransform = surface_caps2.surfaceCapabilities.currentTransform;
	swapchain_info.compositeAlpha = pick_composite_alpha(surface_caps2.surfaceCapabilities.supportedCompositeAlpha);
	swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swapchain_info.clipped = VK_TRUE;
	swapchain_info.oldSwapchain = VK_NULL_HANDLE;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkResult result = vkCreateSwapchainKHR(vulkan.device, &swapchain_info, nullptr, &swapchain);
	if (result == VK_ERROR_INITIALIZATION_FAILED)
	{
		printf("vkCreateSwapchainKHR returned VK_ERROR_INITIALIZATION_FAILED for the selected display surface.\n");
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}
	check(result);

	uint32_t image_count = 0;
	check(vkGetSwapchainImagesKHR(vulkan.device, swapchain, &image_count, nullptr));
	assert(image_count > 0);
	std::vector<VkImage> images(image_count);
	check(vkGetSwapchainImagesKHR(vulkan.device, swapchain, &image_count, images.data()));

	VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	check(vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool));

	VkCommandBufferAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	allocate_info.commandPool = command_pool;
	allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	check(vkAllocateCommandBuffers(vulkan.device, &allocate_info, &command_buffer));

	VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr};
	VkSemaphore image_available = VK_NULL_HANDLE;
	VkSemaphore render_finished = VK_NULL_HANDLE;
	check(vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &image_available));
	check(vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &render_finished));

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);

	bench_start_iteration(vulkan.bench);
	uint32_t image_index = 0;
	result = vkAcquireNextImageKHR(vulkan.device, swapchain, 1000000000, image_available, VK_NULL_HANDLE, &image_index);
	if (result == VK_TIMEOUT || result == VK_NOT_READY || result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		bench_stop_iteration(vulkan.bench);
		vkDestroySemaphore(vulkan.device, render_finished, nullptr);
		vkDestroySemaphore(vulkan.device, image_available, nullptr);
		vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
		vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}
	assert(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
	assert(image_index < images.size());

	VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkBeginCommandBuffer(command_buffer, &begin_info));

	VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr};
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = images[image_index];
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
	                     1, &barrier);
	check(vkEndCommandBuffer(command_buffer));

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &image_available;
	submit_info.pWaitDstStageMask = &wait_stage;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &render_finished;
	check(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

	uint64_t present_id = 1;
	VkPresentId2KHR present_id_info = {VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR, nullptr};
	present_id_info.swapchainCount = 1;
	present_id_info.pPresentIds = &present_id;

	VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, &present_id_info};
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &render_finished;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &swapchain;
	present_info.pImageIndices = &image_index;
	result = vkQueuePresentKHR(queue, &present_info);
	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		assert(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
	}
	MAKEDEVICEPROCADDR(vulkan, vkWaitForPresent2KHR);

	VkPresentWait2InfoKHR wait_info{VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR, nullptr, present_id, 1000000000};

	result = pf_vkWaitForPresent2KHR(vulkan.device, swapchain, &wait_info);
	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		assert(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_TIMEOUT || result == VK_ERROR_OUT_OF_DATE_KHR);
	}

	check(vkQueueWaitIdle(queue));

	bench_stop_iteration(vulkan.bench);
	vkDestroySemaphore(vulkan.device, render_finished, nullptr);
	vkDestroySemaphore(vulkan.device, image_available, nullptr);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
	test_window_destroy(vulkan, window);
	test_done(vulkan);
	return 0;
}
