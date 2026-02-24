#include "vulkan_window_common.h"
#include <algorithm>
#include <inttypes.h>

static vulkan_req_t reqs;

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static VkCompositeAlphaFlagBitsKHR pick_composite_alpha(VkCompositeAlphaFlagsKHR supported)
{
	assert(supported != 0);
	if (supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	assert(!"No supported composite alpha bits reported");
	return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static VkPresentModeKHR pick_present_mode(const std::vector<VkPresentModeKHR>& modes)
{
	for (auto mode : modes)
	{
		if (mode == VK_PRESENT_MODE_FIFO_KHR) return mode;
	}
	assert(!modes.empty());
	return modes[0];
}

static bool is_present_mode_allowed(VkPresentModeKHR mode, bool allow_shared_presentable, bool allow_fifo_latest_ready)
{
	switch (mode)
	{
	case VK_PRESENT_MODE_IMMEDIATE_KHR:
	case VK_PRESENT_MODE_MAILBOX_KHR:
	case VK_PRESENT_MODE_FIFO_KHR:
	case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
		return true;
	case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:
	case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR:
		return allow_shared_presentable;
	case VK_PRESENT_MODE_FIFO_LATEST_READY_EXT:
		return allow_fifo_latest_ready;
	default:
		return false;
	}
}

static VkPresentScalingFlagsEXT pick_scaling_behavior(VkPresentScalingFlagsEXT supported)
{
	if (supported & VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT) return VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT;
	if (supported & VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_EXT) return VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_EXT;
	if (supported & VK_PRESENT_SCALING_STRETCH_BIT_EXT) return VK_PRESENT_SCALING_STRETCH_BIT_EXT;
	return 0;
}

static VkPresentGravityFlagsEXT pick_gravity(VkPresentGravityFlagsEXT supported)
{
	if (supported & VK_PRESENT_GRAVITY_CENTERED_BIT_EXT) return VK_PRESENT_GRAVITY_CENTERED_BIT_EXT;
	if (supported & VK_PRESENT_GRAVITY_MIN_BIT_EXT) return VK_PRESENT_GRAVITY_MIN_BIT_EXT;
	if (supported & VK_PRESENT_GRAVITY_MAX_BIT_EXT) return VK_PRESENT_GRAVITY_MAX_BIT_EXT;
	return 0;
}

int main(int argc, char** argv)
{
	VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, nullptr, VK_TRUE };
	reqs.instance_extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
	reqs.instance_extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
	reqs.instance_extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&swapchain_features);
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_swapchain_maintenance1", reqs);
	VkResult r = VK_SUCCESS;

	testwindow window = test_window_create(vulkan, 0, 0, 400, 300);
	test_set_name(vulkan, VK_OBJECT_TYPE_SURFACE_KHR, (uint64_t)window.surface, "maintenance_surface");

	VkBool32 present_support = VK_FALSE;
	r = vkGetPhysicalDeviceSurfaceSupportKHR(vulkan.physical, 0, window.surface, &present_support);
	check(r);
	if (!present_support)
	{
		printf("Queue family 0 does not support present.\n");
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}

	uint32_t format_count = 0;
	r = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, window.surface, &format_count, nullptr);
	check(r);
	assert(format_count > 0);
	std::vector<VkSurfaceFormatKHR> formats(format_count);
	r = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, window.surface, &format_count, formats.data());
	check(r);
	VkSurfaceFormatKHR surface_format = formats[0];
	if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
	{
		surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
		surface_format.colorSpace = formats[0].colorSpace;
	}

	uint32_t present_mode_count = 0;
	r = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, window.surface, &present_mode_count, nullptr);
	check(r);
	assert(present_mode_count > 0);
	std::vector<VkPresentModeKHR> present_modes(present_mode_count);
	r = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, window.surface, &present_mode_count, present_modes.data());
	check(r);
	VkPresentModeKHR present_mode = pick_present_mode(present_modes);

	VkSurfaceCapabilitiesKHR surface_caps = {};
	r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan.physical, window.surface, &surface_caps);
	check(r);

	VkSurfacePresentModeEXT surface_present_mode = { VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT, nullptr, present_mode };
	VkPhysicalDeviceSurfaceInfo2KHR surface_info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR, &surface_present_mode, window.surface };

	VkSurfacePresentScalingCapabilitiesEXT scaling_caps = {};
	scaling_caps.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT;
	scaling_caps.pNext = nullptr;
	VkSurfacePresentModeCompatibilityEXT compatibility = {};
	compatibility.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_EXT;
	compatibility.pNext = &scaling_caps;
	VkSurfaceCapabilities2KHR surface_caps2 = {};
	surface_caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
	surface_caps2.pNext = &compatibility;

	r = vkGetPhysicalDeviceSurfaceCapabilities2KHR(vulkan.physical, &surface_info, &surface_caps2);
	check(r);
	std::vector<VkPresentModeKHR> compat_modes;
	if (compatibility.presentModeCount > 0)
	{
		compat_modes.resize(compatibility.presentModeCount);
		compatibility.pPresentModes = compat_modes.data();
		scaling_caps = {};
		scaling_caps.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT;
		scaling_caps.pNext = nullptr;
		compatibility = {};
		compatibility.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_EXT;
		compatibility.pNext = &scaling_caps;
		compatibility.pPresentModes = compat_modes.data();
		surface_caps2 = {};
		surface_caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
		surface_caps2.pNext = &compatibility;
		r = vkGetPhysicalDeviceSurfaceCapabilities2KHR(vulkan.physical, &surface_info, &surface_caps2);
		check(r);
	}
	assert(!compat_modes.empty());
	bool present_mode_ok = false;
	for (auto mode : compat_modes)
	{
		if (mode == present_mode)
		{
			present_mode_ok = true;
			break;
		}
	}
	if (!present_mode_ok) present_mode = compat_modes[0];

	std::vector<VkPresentModeKHR> swapchain_present_modes = compat_modes;
	if (swapchain_present_modes.empty()) swapchain_present_modes = present_modes;

	const bool allow_shared_presentable = (vulkan.device_extensions.count("VK_KHR_shared_presentable_image") > 0);
	const bool allow_fifo_latest_ready = (vulkan.device_extensions.count("VK_EXT_present_mode_fifo_latest_ready") > 0);
	std::vector<VkPresentModeKHR> filtered_present_modes;
	filtered_present_modes.reserve(swapchain_present_modes.size());
	for (auto mode : swapchain_present_modes)
	{
		if (is_present_mode_allowed(mode, allow_shared_presentable, allow_fifo_latest_ready))
		{
			filtered_present_modes.push_back(mode);
		}
	}
	assert(!filtered_present_modes.empty());
	swapchain_present_modes.swap(filtered_present_modes);

	VkPresentScalingFlagsEXT scaling_behavior = pick_scaling_behavior(scaling_caps.supportedPresentScaling);
	VkPresentGravityFlagsEXT gravity_x = 0;
	VkPresentGravityFlagsEXT gravity_y = 0;
	if (scaling_behavior != 0)
	{
		gravity_x = pick_gravity(scaling_caps.supportedPresentGravityX);
		gravity_y = pick_gravity(scaling_caps.supportedPresentGravityY);
	}

	VkSwapchainPresentModesCreateInfoEXT present_modes_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT, nullptr };
	present_modes_info.presentModeCount = static_cast<uint32_t>(swapchain_present_modes.size());
	present_modes_info.pPresentModes = swapchain_present_modes.data();

	VkSwapchainPresentScalingCreateInfoEXT scaling_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT, &present_modes_info };
	scaling_info.scalingBehavior = scaling_behavior;
	scaling_info.presentGravityX = gravity_x;
	scaling_info.presentGravityY = gravity_y;
	void* swapchain_pnext = &present_modes_info;
	if (scaling_behavior != 0)
	{
		swapchain_pnext = &scaling_info;
	}

	VkExtent2D extent = surface_caps.currentExtent;
	if (extent.width == UINT32_MAX)
	{
		extent.width = 400;
		extent.height = 300;
	}
	if (scaling_behavior != 0)
	{
		extent.width = std::max(scaling_caps.minScaledImageExtent.width, std::min(extent.width, scaling_caps.maxScaledImageExtent.width));
		extent.height = std::max(scaling_caps.minScaledImageExtent.height, std::min(extent.height, scaling_caps.maxScaledImageExtent.height));
	}

	uint32_t desired_images = surface_caps.minImageCount + 1;
	if (surface_caps.maxImageCount > 0 && desired_images > surface_caps.maxImageCount)
	{
		desired_images = surface_caps.maxImageCount;
	}

	VkCompositeAlphaFlagBitsKHR composite_alpha = pick_composite_alpha(surface_caps.supportedCompositeAlpha);
	VkSwapchainCreateInfoKHR swapchain_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, swapchain_pnext };
	swapchain_info.surface = window.surface;
	swapchain_info.minImageCount = desired_images;
	swapchain_info.imageFormat = surface_format.format;
	swapchain_info.imageColorSpace = surface_format.colorSpace;
	swapchain_info.imageExtent = extent;
	swapchain_info.imageArrayLayers = 1;
	swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_info.preTransform = surface_caps.currentTransform;
	swapchain_info.compositeAlpha = composite_alpha;
	swapchain_info.presentMode = present_mode;
	swapchain_info.clipped = VK_TRUE;
	swapchain_info.oldSwapchain = VK_NULL_HANDLE;

	bench_start_iteration(vulkan.bench);

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	r = vkCreateSwapchainKHR(vulkan.device, &swapchain_info, nullptr, &swapchain);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain, "swapchain_maintenance1");
	test_marker_mention(vulkan, "Created swapchain", VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain);

	uint32_t image_count = 0;
	r = vkGetSwapchainImagesKHR(vulkan.device, swapchain, &image_count, nullptr);
	check(r);
	assert(image_count > 0);
	std::vector<VkImage> images(image_count);
	r = vkGetSwapchainImagesKHR(vulkan.device, swapchain, &image_count, images.data());
	check(r);
	for (uint32_t i = 0; i < image_count; ++i)
	{
		char name[64];
		snprintf(name, sizeof(name), "swapchain_image_%" PRIu32, i);
		test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)images[i], name);
	}

	VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	VkSemaphore acquire_semaphore = VK_NULL_HANDLE;
	r = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &acquire_semaphore);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)acquire_semaphore, "swapchain_acquire_semaphore");
	VkSemaphore render_semaphore = VK_NULL_HANDLE;
	r = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &render_semaphore);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)render_semaphore, "swapchain_render_semaphore");

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence present_fence = VK_NULL_HANDLE;
	r = vkCreateFence(vulkan.device, &fence_info, nullptr, &present_fence);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_FENCE, (uint64_t)present_fence, "swapchain_present_fence");

	VkFence acquire_fence = VK_NULL_HANDLE;
	r = vkCreateFence(vulkan.device, &fence_info, nullptr, &acquire_fence);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_FENCE, (uint64_t)acquire_fence, "swapchain_release_fence");

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	r = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)command_pool, "swapchain_command_pool");

	VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	alloc_info.commandPool = command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	r = vkAllocateCommandBuffers(vulkan.device, &alloc_info, &command_buffer);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer, "swapchain_command_buffer");

	uint32_t image_index = 0;
	r = vkAcquireNextImageKHR(vulkan.device, swapchain, UINT64_MAX, acquire_semaphore, VK_NULL_HANDLE, &image_index);
	if (r == VK_SUBOPTIMAL_KHR) r = VK_SUCCESS;
	if (r == VK_ERROR_OUT_OF_DATE_KHR)
	{
		printf("Swapchain out of date during acquire.\n");
		vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
		vkDestroyFence(vulkan.device, acquire_fence, nullptr);
		vkDestroyFence(vulkan.device, present_fence, nullptr);
		vkDestroySemaphore(vulkan.device, acquire_semaphore, nullptr);
		vkDestroySemaphore(vulkan.device, render_semaphore, nullptr);
		vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}
	check(r);

	VkSwapchainPresentFenceInfoEXT present_fence_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT, nullptr, 1, &present_fence };
	VkSwapchainPresentModeInfoEXT present_mode_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT, &present_fence_info, 1, &present_mode };
	VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, &present_mode_info };
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &render_semaphore;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &swapchain;
	present_info.pImageIndices = &image_index;

	test_marker_mention(vulkan, "Presenting swapchain image", VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	r = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(r);
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = 0;
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
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	r = vkEndCommandBuffer(command_buffer);
	check(r);

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &acquire_semaphore;
	submit_info.pWaitDstStageMask = &wait_stage;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &render_semaphore;
	r = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(r);

	r = vkQueuePresentKHR(queue, &present_info);
	if (r == VK_SUBOPTIMAL_KHR) r = VK_SUCCESS;
	if (r == VK_ERROR_OUT_OF_DATE_KHR)
	{
		printf("Swapchain out of date during present.\n");
		vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
		vkDestroyFence(vulkan.device, acquire_fence, nullptr);
		vkDestroyFence(vulkan.device, present_fence, nullptr);
		vkDestroySemaphore(vulkan.device, acquire_semaphore, nullptr);
		vkDestroySemaphore(vulkan.device, render_semaphore, nullptr);
		vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}
	check(r);

	r = vkWaitForFences(vulkan.device, 1, &present_fence, VK_TRUE, UINT64_MAX);
	check(r);
	r = vkResetFences(vulkan.device, 1, &present_fence);
	check(r);

	r = vkAcquireNextImageKHR(vulkan.device, swapchain, UINT64_MAX, VK_NULL_HANDLE, acquire_fence, &image_index);
	if (r == VK_SUBOPTIMAL_KHR) r = VK_SUCCESS;
	if (r == VK_ERROR_OUT_OF_DATE_KHR)
	{
		printf("Swapchain out of date during release acquire.\n");
		vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
		vkDestroyFence(vulkan.device, acquire_fence, nullptr);
		vkDestroyFence(vulkan.device, present_fence, nullptr);
		vkDestroySemaphore(vulkan.device, acquire_semaphore, nullptr);
		vkDestroySemaphore(vulkan.device, render_semaphore, nullptr);
		vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}
	check(r);

	r = vkWaitForFences(vulkan.device, 1, &acquire_fence, VK_TRUE, UINT64_MAX);
	check(r);

	MAKEDEVICEPROCADDR(vulkan, vkReleaseSwapchainImagesEXT);
	VkReleaseSwapchainImagesInfoEXT release_info = { VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT, nullptr };
	release_info.swapchain = swapchain;
	release_info.imageIndexCount = 1;
	release_info.pImageIndices = &image_index;
	r = pf_vkReleaseSwapchainImagesEXT(vulkan.device, &release_info);
	if (r == VK_ERROR_OUT_OF_DATE_KHR)
	{
		printf("Swapchain out of date during release.\n");
		vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
		vkDestroyFence(vulkan.device, acquire_fence, nullptr);
		vkDestroyFence(vulkan.device, present_fence, nullptr);
		vkDestroySemaphore(vulkan.device, acquire_semaphore, nullptr);
		vkDestroySemaphore(vulkan.device, render_semaphore, nullptr);
		vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}
	check(r);
	test_marker_mention(vulkan, "Released swapchain image", VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain);

	r = vkQueueWaitIdle(queue);
	check(r);

	bench_stop_iteration(vulkan.bench);

	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyFence(vulkan.device, acquire_fence, nullptr);
	vkDestroyFence(vulkan.device, present_fence, nullptr);
	vkDestroySemaphore(vulkan.device, acquire_semaphore, nullptr);
	vkDestroySemaphore(vulkan.device, render_semaphore, nullptr);
	vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
	test_window_destroy(vulkan, window);
	test_done(vulkan);

	return 0;
}
