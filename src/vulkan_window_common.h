#pragma once

#include "vulkan_common.h"

#if VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
#include <xcb/randr.h>

struct testwindow
{
	xcb_connection_t* connection = nullptr;
	xcb_screen_t* screen = nullptr;
	xcb_window_t window = 0;
	xcb_atom_t protocol_atom;
	xcb_atom_t delete_window_atom;
	xcb_atom_t state_atom;
	xcb_atom_t state_fullscreen_atom;
	xcb_atom_t bypass_compositor_atom;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	bool fullscreen = false;
};

#else

struct testwindow
{
	VkSurfaceKHR surface = VK_NULL_HANDLE;
};

#endif

testwindow test_window_create(const vulkan_setup_t& vulkan, int32_t x, int32_t y, int32_t width, int32_t height, bool fullscreen = false);
void test_window_destroy(const vulkan_setup_t& vulkan, testwindow &w);
