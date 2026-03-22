#include "vulkan_common.h"

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
	for (VkPresentModeKHR mode : modes)
	{
		if (mode == VK_PRESENT_MODE_FIFO_KHR) return mode;
	}
	assert(!modes.empty());
	return modes[0];
}

static VkImageUsageFlags pick_image_usage(VkImageUsageFlags supported)
{
	if (supported & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (supported & VK_IMAGE_USAGE_TRANSFER_DST_BIT) return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (supported & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (supported & VK_IMAGE_USAGE_STORAGE_BIT) return VK_IMAGE_USAGE_STORAGE_BIT;
	if (supported & VK_IMAGE_USAGE_SAMPLED_BIT) return VK_IMAGE_USAGE_SAMPLED_BIT;
	assert(!"No supported swapchain image usage bits reported");
	return 0;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.instance_extensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_headless_surface", reqs);
	VkResult r = VK_SUCCESS;

	MAKEINSTANCEPROCADDR(vulkan, vkCreateHeadlessSurfaceEXT);

	VkHeadlessSurfaceCreateInfoEXT surface_info = { VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT, nullptr };
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	r = pf_vkCreateHeadlessSurfaceEXT(vulkan.instance, &surface_info, nullptr, &surface);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_SURFACE_KHR, (uint64_t)surface, "headless_surface");
	test_marker_mention(vulkan, "Created headless surface", VK_OBJECT_TYPE_SURFACE_KHR, (uint64_t)surface);

	VkBool32 present_support = VK_FALSE;
	r = vkGetPhysicalDeviceSurfaceSupportKHR(vulkan.physical, 0, surface, &present_support);
	check(r);
	if (!present_support)
	{
		printf("Queue family 0 does not support present on the headless surface.\n");
		vkDestroySurfaceKHR(vulkan.instance, surface, nullptr);
		test_done(vulkan);
		return 77;
	}

	uint32_t format_count = 0;
	r = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, surface, &format_count, nullptr);
	check(r);
	assert(format_count > 0);
	std::vector<VkSurfaceFormatKHR> formats(format_count);
	r = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, surface, &format_count, formats.data());
	check(r);
	VkSurfaceFormatKHR surface_format = formats[0];
	if (format_count == 1 && surface_format.format == VK_FORMAT_UNDEFINED)
	{
		surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
	}

	uint32_t present_mode_count = 0;
	r = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, surface, &present_mode_count, nullptr);
	check(r);
	assert(present_mode_count > 0);
	std::vector<VkPresentModeKHR> present_modes(present_mode_count);
	r = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, surface, &present_mode_count, present_modes.data());
	check(r);

	VkSurfaceCapabilitiesKHR surface_caps = {};
	r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan.physical, surface, &surface_caps);
	check(r);

	VkExtent2D extent = surface_caps.currentExtent;
	if (extent.width == UINT32_MAX)
	{
		extent.width = 64;
		extent.height = 64;
	}

	uint32_t image_count = surface_caps.minImageCount < 2 ? 2 : surface_caps.minImageCount;
	if (surface_caps.maxImageCount > 0 && image_count > surface_caps.maxImageCount)
	{
		image_count = surface_caps.maxImageCount;
	}
	assert(image_count >= surface_caps.minImageCount);

	VkSwapchainCreateInfoKHR swapchain_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr };
	swapchain_info.surface = surface;
	swapchain_info.minImageCount = image_count;
	swapchain_info.imageFormat = surface_format.format;
	swapchain_info.imageColorSpace = surface_format.colorSpace;
	swapchain_info.imageExtent = extent;
	swapchain_info.imageArrayLayers = 1;
	swapchain_info.imageUsage = pick_image_usage(surface_caps.supportedUsageFlags);
	swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_info.preTransform = surface_caps.currentTransform;
	swapchain_info.compositeAlpha = pick_composite_alpha(surface_caps.supportedCompositeAlpha);
	swapchain_info.presentMode = pick_present_mode(present_modes);
	swapchain_info.clipped = VK_TRUE;
	swapchain_info.oldSwapchain = VK_NULL_HANDLE;

	bench_start_iteration(vulkan.bench);

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	r = vkCreateSwapchainKHR(vulkan.device, &swapchain_info, nullptr, &swapchain);
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain, "headless_swapchain");
	test_marker_mention(vulkan, "Created headless swapchain", VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain);

	uint32_t swapchain_image_count = 0;
	r = vkGetSwapchainImagesKHR(vulkan.device, swapchain, &swapchain_image_count, nullptr);
	check(r);
	assert(swapchain_image_count > 0);
	std::vector<VkImage> images(swapchain_image_count);
	r = vkGetSwapchainImagesKHR(vulkan.device, swapchain, &swapchain_image_count, images.data());
	check(r);

	bench_stop_iteration(vulkan.bench);

	vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
	vkDestroySurfaceKHR(vulkan.instance, surface, nullptr);
	test_done(vulkan);
	return 0;
}
