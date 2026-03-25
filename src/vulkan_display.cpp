#include "vulkan_common.h"

static bool create_display_mode = false;

struct display_candidate_t
{
	uint32_t display_index = 0;
	uint32_t plane_index = 0;
	VkDisplayPropertiesKHR display = {};
	VkDisplayPlanePropertiesKHR plane = {};
	VkDisplayModePropertiesKHR mode = {};
	VkDisplayPlaneCapabilitiesKHR capabilities = {};
};

static void show_usage()
{
	printf("--create-mode          Exercise vkCreateDisplayModeKHR before creating the surface\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)argc;
	(void)reqs;
	if (match(argv[i], nullptr, "--create-mode"))
	{
		create_display_mode = true;
		return true;
	}
	return false;
}

static bool contains_display(const std::vector<VkDisplayKHR>& displays, VkDisplayKHR display)
{
	for (VkDisplayKHR candidate : displays)
	{
		if (candidate == display) return true;
	}
	return false;
}

static bool extent_in_range(const VkExtent2D& extent, const VkExtent2D& min_extent, const VkExtent2D& max_extent)
{
	return extent.width >= min_extent.width &&
	       extent.width <= max_extent.width &&
	       extent.height >= min_extent.height &&
	       extent.height <= max_extent.height;
}

static VkDisplayPlaneAlphaFlagBitsKHR pick_display_alpha(VkDisplayPlaneAlphaFlagsKHR supported)
{
	assert(supported != 0);
	if (supported & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR) return VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
	if (supported & VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR) return VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
	if (supported & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR) return VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
	if (supported & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR) return VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
	assert(!"No supported display alpha modes reported");
	return VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
}

static VkSurfaceTransformFlagBitsKHR pick_surface_transform(VkSurfaceTransformFlagsKHR supported)
{
	assert(supported != 0);
	if (supported & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	if (supported & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) return VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
	if (supported & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) return VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR;
	if (supported & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) return VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
	if (supported & VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR) return VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR;
	if (supported & VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR) return VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR;
	if (supported & VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR) return VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR;
	if (supported & VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR) return VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR;
	if (supported & VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR) return VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR;
	assert(!"No supported display transforms reported");
	return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
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

static bool find_candidate(
	const vulkan_setup_t& vulkan,
	PFN_vkGetDisplayPlaneSupportedDisplaysKHR get_display_plane_supported_displays,
	PFN_vkGetDisplayModePropertiesKHR get_display_mode_properties,
	PFN_vkGetDisplayPlaneCapabilitiesKHR get_display_plane_capabilities,
	const std::vector<VkDisplayPropertiesKHR>& displays,
	const std::vector<VkDisplayPlanePropertiesKHR>& planes,
	display_candidate_t& selected)
{
	for (uint32_t plane_index = 0; plane_index < planes.size(); ++plane_index)
	{
		uint32_t supported_display_count = 0;
		VkResult r = get_display_plane_supported_displays(vulkan.physical, plane_index, &supported_display_count, nullptr);
		check(r);
		if (supported_display_count == 0) continue;

		std::vector<VkDisplayKHR> supported_displays(supported_display_count);
		r = get_display_plane_supported_displays(vulkan.physical, plane_index, &supported_display_count, supported_displays.data());
		check(r);

		for (uint32_t display_index = 0; display_index < displays.size(); ++display_index)
		{
			const VkDisplayPropertiesKHR& display = displays[display_index];
			const VkDisplayPlanePropertiesKHR& plane = planes[plane_index];
			if (!contains_display(supported_displays, display.display)) continue;
			if (plane.currentDisplay != VK_NULL_HANDLE && plane.currentDisplay != display.display) continue;
			if (display.supportedTransforms == 0) continue;

			uint32_t mode_count = 0;
			r = get_display_mode_properties(vulkan.physical, display.display, &mode_count, nullptr);
			check(r);
			if (mode_count == 0) continue;

			std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
			r = get_display_mode_properties(vulkan.physical, display.display, &mode_count, modes.data());
			check(r);

			for (const VkDisplayModePropertiesKHR& mode : modes)
			{
				const VkExtent2D extent = mode.parameters.visibleRegion;
				if (extent.width == 0 || extent.height == 0) continue;
				if (extent.width > vulkan.device_properties.limits.maxImageDimension2D) continue;
				if (extent.height > vulkan.device_properties.limits.maxImageDimension2D) continue;

				VkDisplayPlaneCapabilitiesKHR capabilities = {};
				r = get_display_plane_capabilities(vulkan.physical, mode.displayMode, plane_index, &capabilities);
				check(r);
				if (capabilities.supportedAlpha == 0) continue;
				if (!extent_in_range(extent, capabilities.minSrcExtent, capabilities.maxSrcExtent)) continue;
				if (!extent_in_range(extent, capabilities.minDstExtent, capabilities.maxDstExtent)) continue;

				selected.display_index = display_index;
				selected.plane_index = plane_index;
				selected.display = display;
				selected.plane = plane;
				selected.mode = mode;
				selected.capabilities = capabilities;
				return true;
			}
		}
	}
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.instance_extensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_display", reqs);
	VkResult r = VK_SUCCESS;

	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceDisplayPropertiesKHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetDisplayPlaneSupportedDisplaysKHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetDisplayModePropertiesKHR);
	MAKEINSTANCEPROCADDR(vulkan, vkCreateDisplayModeKHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetDisplayPlaneCapabilitiesKHR);
	MAKEINSTANCEPROCADDR(vulkan, vkCreateDisplayPlaneSurfaceKHR);

	uint32_t display_count = 0;
	r = pf_vkGetPhysicalDeviceDisplayPropertiesKHR(vulkan.physical, &display_count, nullptr);
	check(r);
	if (display_count == 0)
	{
		printf("VK_KHR_display is present, but no displays were reported.\n");
		test_done(vulkan);
		return 77;
	}

	std::vector<VkDisplayPropertiesKHR> displays(display_count);
	r = pf_vkGetPhysicalDeviceDisplayPropertiesKHR(vulkan.physical, &display_count, displays.data());
	check(r);

	uint32_t plane_count = 0;
	r = pf_vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vulkan.physical, &plane_count, nullptr);
	check(r);
	if (plane_count == 0)
	{
		printf("VK_KHR_display is present, but no display planes were reported.\n");
		test_done(vulkan);
		return 77;
	}

	std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
	r = pf_vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vulkan.physical, &plane_count, planes.data());
	check(r);

	display_candidate_t candidate;
	if (!find_candidate(vulkan, pf_vkGetDisplayPlaneSupportedDisplaysKHR, pf_vkGetDisplayModePropertiesKHR,
	                    pf_vkGetDisplayPlaneCapabilitiesKHR, displays, planes, candidate))
	{
		printf("No compatible VK_KHR_display display/plane/mode combination was found.\n");
		test_done(vulkan);
		return 77;
	}

	const char* display_name = candidate.display.displayName ? candidate.display.displayName : "<unnamed>";
	printf("Using display %u (%s), plane %u, mode %ux%u @ %u mHz\n",
		candidate.display_index,
		display_name,
		candidate.plane_index,
		candidate.mode.parameters.visibleRegion.width,
		candidate.mode.parameters.visibleRegion.height,
		candidate.mode.parameters.refreshRate);

	test_set_name(vulkan, VK_OBJECT_TYPE_DISPLAY_KHR, (uint64_t)candidate.display.display, "display");
	test_set_name(vulkan, VK_OBJECT_TYPE_DISPLAY_MODE_KHR, (uint64_t)candidate.mode.displayMode, "display_mode");
	test_marker_mention(vulkan, "Selected display", VK_OBJECT_TYPE_DISPLAY_KHR, (uint64_t)candidate.display.display);

	bench_start_iteration(vulkan.bench);

	VkDisplayModeKHR surface_mode = candidate.mode.displayMode;
	if (create_display_mode)
	{
		VkDisplayModeCreateInfoKHR display_mode_info = { VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR, nullptr };
		display_mode_info.parameters = candidate.mode.parameters;
		VkDisplayModeKHR created_mode = VK_NULL_HANDLE;
		r = pf_vkCreateDisplayModeKHR(vulkan.physical, candidate.display.display, &display_mode_info, nullptr, &created_mode);
		if (r == VK_ERROR_INITIALIZATION_FAILED)
		{
			printf("vkCreateDisplayModeKHR failed for the selected display mode.\n");
			bench_stop_iteration(vulkan.bench);
			test_done(vulkan);
			return 77;
		}
		check(r);
		surface_mode = created_mode;
		test_set_name(vulkan, VK_OBJECT_TYPE_DISPLAY_MODE_KHR, (uint64_t)surface_mode, "created_display_mode");
		test_marker_mention(vulkan, "Created display mode", VK_OBJECT_TYPE_DISPLAY_MODE_KHR, (uint64_t)surface_mode);
	}

	VkDisplaySurfaceCreateInfoKHR surface_info = { VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR, nullptr };
	surface_info.displayMode = surface_mode;
	surface_info.planeIndex = candidate.plane_index;
	surface_info.planeStackIndex = candidate.plane.currentStackIndex;
	surface_info.transform = pick_surface_transform(candidate.display.supportedTransforms);
	surface_info.alphaMode = pick_display_alpha(candidate.capabilities.supportedAlpha);
	surface_info.globalAlpha = (surface_info.alphaMode == VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR) ? 1.0f : 0.0f;
	surface_info.imageExtent = candidate.mode.parameters.visibleRegion;

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	r = pf_vkCreateDisplayPlaneSurfaceKHR(vulkan.instance, &surface_info, nullptr, &surface);
	if (r == VK_ERROR_INITIALIZATION_FAILED)
	{
		printf("vkCreateDisplayPlaneSurfaceKHR failed for the selected display plane.\n");
		bench_stop_iteration(vulkan.bench);
		test_done(vulkan);
		return 77;
	}
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_SURFACE_KHR, (uint64_t)surface, "display_surface");
	test_marker_mention(vulkan, "Created display surface", VK_OBJECT_TYPE_SURFACE_KHR, (uint64_t)surface);

	VkBool32 present_support = VK_FALSE;
	r = vkGetPhysicalDeviceSurfaceSupportKHR(vulkan.physical, 0, surface, &present_support);
	check(r);
	if (!present_support)
	{
		printf("Queue family 0 does not support present on the display surface.\n");
		bench_stop_iteration(vulkan.bench);
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
	if (extent.width == UINT32_MAX || extent.height == UINT32_MAX)
	{
		extent = candidate.mode.parameters.visibleRegion;
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

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	r = vkCreateSwapchainKHR(vulkan.device, &swapchain_info, nullptr, &swapchain);
	if (r == VK_ERROR_INITIALIZATION_FAILED)
	{
		printf("vkCreateSwapchainKHR returned VK_ERROR_INITIALIZATION_FAILED for the selected display surface.\n");
		bench_stop_iteration(vulkan.bench);
		vkDestroySurfaceKHR(vulkan.instance, surface, nullptr);
		test_done(vulkan);
		return 77;
	}
	check(r);
	test_set_name(vulkan, VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain, "display_swapchain");
	test_marker_mention(vulkan, "Created display swapchain", VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain);

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
