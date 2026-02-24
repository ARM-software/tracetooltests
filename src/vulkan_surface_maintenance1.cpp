#include "vulkan_window_common.h"
#include <inttypes.h>
#include <vector>

static vulkan_req_t reqs;

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static bool is_special_extent(const VkExtent2D& extent)
{
	return extent.width == UINT32_MAX && extent.height == UINT32_MAX;
}

int main(int argc, char** argv)
{
	const char* winsys = getenv("TOOLSTEST_WINSYS");
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	if (winsys && strcmp(winsys, "headless") == 0)
	{
		reqs.instance_extensions.push_back("VK_EXT_headless_surface");
	}
	else
	{
		reqs.instance_extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
	}
	reqs.instance_extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
	reqs.instance_extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_surface_maintenance1", reqs);
	VkResult r = VK_SUCCESS;

	testwindow window = test_window_create(vulkan, 0, 0, 400, 300);

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

	uint32_t present_mode_count = 0;
	r = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, window.surface, &present_mode_count, nullptr);
	check(r);
	assert(present_mode_count > 0);
	std::vector<VkPresentModeKHR> present_modes(present_mode_count);
	r = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, window.surface, &present_mode_count, present_modes.data());
	check(r);

	bench_start_iteration(vulkan.bench);

	for (uint32_t i = 0; i < present_mode_count; ++i)
	{
		VkPresentModeKHR present_mode = present_modes[i];
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
		assert(compatibility.presentModeCount > 0);

		std::vector<VkPresentModeKHR> compatible_modes(compatibility.presentModeCount);
		compatibility.presentModeCount = static_cast<uint32_t>(compatible_modes.size());
		compatibility.pPresentModes = compatible_modes.data();
		scaling_caps = {};
		scaling_caps.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT;
		scaling_caps.pNext = nullptr;
		compatibility.pNext = &scaling_caps;
		surface_caps2.pNext = &compatibility;

		r = vkGetPhysicalDeviceSurfaceCapabilities2KHR(vulkan.physical, &surface_info, &surface_caps2);
		check(r);

		bool present_mode_found = false;
		for (auto mode : compatible_modes)
		{
			if (mode == present_mode)
			{
				present_mode_found = true;
				break;
			}
		}
		assert(present_mode_found);

		const VkSurfaceCapabilitiesKHR& caps = surface_caps2.surfaceCapabilities;
		if (caps.maxImageCount != 0)
		{
			assert(caps.minImageCount <= caps.maxImageCount);
		}
		if (!is_special_extent(scaling_caps.minScaledImageExtent) && !is_special_extent(scaling_caps.maxScaledImageExtent))
		{
			assert(scaling_caps.minScaledImageExtent.width <= scaling_caps.maxScaledImageExtent.width);
			assert(scaling_caps.minScaledImageExtent.height <= scaling_caps.maxScaledImageExtent.height);
		}

		printf("Present mode %" PRIu32 ": minImages=%" PRIu32 " maxImages=%" PRIu32 " compatCount=%" PRIu32 " scaling=0x%" PRIx32 " gravityX=0x%" PRIx32 " gravityY=0x%" PRIx32 "\n",
			present_mode,
			caps.minImageCount,
			caps.maxImageCount,
			compatibility.presentModeCount,
			scaling_caps.supportedPresentScaling,
			scaling_caps.supportedPresentGravityX,
			scaling_caps.supportedPresentGravityY);
	}

	bench_stop_iteration(vulkan.bench);

	test_marker_mention(vulkan, "Finished surface maintenance queries", VK_OBJECT_TYPE_SURFACE_KHR, (uint64_t)window.surface);
	test_window_destroy(vulkan, window);
	test_done(vulkan);

	return 0;
}
