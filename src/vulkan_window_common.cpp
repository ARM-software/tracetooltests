#include "vulkan_window_common.h"
#include "util.h"

#include <string>
#include <cstring>

#if USE_XCB
#include <xcb/xcb.h>
#include <xcb/randr.h>

static const char* lavaxcb_strerror(int err)
{
	if (err == XCB_CONN_ERROR) return "Connection error";
	else if (err == XCB_CONN_CLOSED_EXT_NOTSUPPORTED) return "Extension not supported";
	else if (err == XCB_CONN_CLOSED_MEM_INSUFFICIENT) return "Insufficient memory";
	else if (err == XCB_CONN_CLOSED_REQ_LEN_EXCEED) return "Request length exceeded";
	else if (err == XCB_CONN_CLOSED_PARSE_ERR) return "Parse error";
	else if (err == XCB_CONN_CLOSED_INVALID_SCREEN) return "Invalid screen";
	else if (err == XCB_CONN_CLOSED_FDPASSING_FAILED) return "Invalid file descriptor";
	else return "Unknown error";
}

static xcb_intern_atom_cookie_t lavaxcb_send_atom_request(xcb_connection_t* connection, const char* name, uint8_t only_if_exists)
{
	return xcb_intern_atom(connection, only_if_exists, strlen(name), name);
}

static xcb_atom_t lavaxcb_get_atom_reply(xcb_connection_t* connection, const char* name, xcb_intern_atom_cookie_t cookie)
{
	xcb_atom_t atom  = 0;
	xcb_generic_error_t* error = nullptr;
	xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection, cookie, &error);

	if (reply != nullptr)
	{
		atom = reply->atom;
		free(reply);
	}
	else
	{
		ELOG("Failed to retrieve internal XCB atom for %s with error %u", name, error->error_code);
		free(error);
	}

	return atom;
}

// TBD fix race condition for multi-window; also message could be something we don't want to miss or for other window
static bool lavaxcb_wait(xcb_connection_t* connection, uint32_t type)
{
	do
	{
		const xcb_generic_event_t* event = xcb_poll_for_event(connection);
		if (event == nullptr) return false;
		if (event->response_type == 0) return false;
		const uint8_t event_code = event->response_type & 0x7f;
		if (event_code == XCB_DESTROY_NOTIFY) return false;
		if (event_code == type) return true;
		usleep(1);
	} while (true);
	return false;
}
#endif

static void window_fullscreen(testwindow& w, bool value)
{
#ifdef USE_XCB
	if (value != w.fullscreen)
	{
		if (value) ILOG("Entering fullscreen mode");
		xcb_client_message_event_t event;
		event.response_type = XCB_CLIENT_MESSAGE;
		event.format = 32;
		event.sequence = 0;
		event.window = w.window;
		event.type = w.state_atom;
		event.data.data32[0] = value ? 1 : 0;
		event.data.data32[1] = w.state_fullscreen_atom;
		event.data.data32[2] = 0;
		event.data.data32[3] = 0;
		event.data.data32[4] = 0;
		xcb_send_event(w.connection, 0, w.screen->root, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char*)&event);
		xcb_flush(w.connection);
		if (lavaxcb_wait(w.connection, XCB_CONFIGURE_NOTIFY))
		{
			w.fullscreen = value;
			const int32_t bypass = value ? 2 : 0; // bypass compositor to workaround VK_ERROR_OUT_OF_DATE_KHR on GNOME + NVIDIA
			xcb_change_property(w.connection, XCB_PROP_MODE_REPLACE, w.window, w.bypass_compositor_atom, XCB_ATOM_CARDINAL, 32, 1, &bypass);
			xcb_flush(w.connection);
			usleep(50000); // not sure if needed, but gfxreconstruct does this so why not
		}
		else ELOG("Failed to %s fullscreen mode", value ? "enter" : "leave");
	}
#endif
	(void)w;
	(void)value;
}

testwindow test_window_create(const vulkan_setup_t& vulkan, int32_t x, int32_t y, int32_t width, int32_t height, bool fullscreen)
{
#if USE_XCB
	testwindow xcb = {};
	int scr = 0;
	xcb.connection = xcb_connect(nullptr, &scr);
	int err = xcb_connection_has_error(xcb.connection);
	if (err)
	{
		ABORT("Failed to connect to XCB server: %s (%d)", lavaxcb_strerror(err), err);
	}
	const xcb_setup_t *setup = xcb_get_setup(xcb.connection);
	if (!setup) ABORT("Failed to setup XCB connection");
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	while (scr-- > 0) xcb_screen_next(&iter);
	xcb.screen = iter.data;
	uint32_t value_mask, value_list[32];
	value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	value_list[0] = xcb.screen->black_pixel;
	value_list[1] = XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE;
	xcb_generic_error_t *error = nullptr;
	xcb.window = xcb_generate_id(xcb.connection);

	// Get screen dimensions
	xcb_get_geometry_cookie_t geomcookie = xcb_get_geometry(xcb.connection, xcb.screen->root);
	xcb_get_geometry_reply_t *geomreply;
	if ((geomreply = xcb_get_geometry_reply(xcb.connection, geomcookie, &error)))
	{
		DLOG("XCB screen size is %d x %d, window is %d x %d, with border=%d\n", geomreply->width, geomreply->height, width, height, (int)geomreply->border_width);
		if (geomreply->width == width || geomreply->height == height) // go fullscreen?
		{
			x = 0;
			y = 0;
			fullscreen = true;
		}
		else if (geomreply->width < width || geomreply->height < height) // warn if screen is too small
		{
			ELOG("Screen is smaller than window - this may be a problem for replay.");
		}
		free(geomreply);
	}
	else
	{
		ELOG("Failed to get screen geometry: %u", error->error_code);
		free(error);
	}

	xcb_void_cookie_t cookie = xcb_create_window_checked(xcb.connection, XCB_COPY_FROM_PARENT, xcb.window, xcb.screen->root, 0, 0, width, height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, xcb.screen->root_visual, value_mask, value_list);
	if ((error = xcb_request_check(xcb.connection, cookie)))
	{
		ABORT("Failed to create XCB window (w=%u, h=%u): %s (%d)", (unsigned)width, (unsigned)height, lavaxcb_strerror(error->error_code), error->error_code);
		free(error);
	}
	cookie = xcb_map_window_checked(xcb.connection, xcb.window);
	if ((error = xcb_request_check(xcb.connection, cookie)))
	{
		ABORT("Failed to show XCB window: %u", error->error_code);
		free(error);
	}

	// grab us some atoms
	xcb_intern_atom_cookie_t protocol_atom_cookie = lavaxcb_send_atom_request(xcb.connection, "WM_PROTOCOLS", 1);
	xcb_intern_atom_cookie_t delete_window_atom_cookie = lavaxcb_send_atom_request(xcb.connection, "WM_DELETE_WINDOW", 0);
	xcb_intern_atom_cookie_t state_atom_cookie = lavaxcb_send_atom_request(xcb.connection, "_NET_WM_STATE", 1);
	xcb_intern_atom_cookie_t state_fullscreen_atom_cookie = lavaxcb_send_atom_request(xcb.connection, "_NET_WM_STATE_FULLSCREEN", 0);
	xcb_intern_atom_cookie_t bypass_compositor_atom_cookie = lavaxcb_send_atom_request(xcb.connection, "_NET_WM_BYPASS_COMPOSITOR", 0);
	xcb.protocol_atom = lavaxcb_get_atom_reply(xcb.connection, "WM_PROTOCOLS", protocol_atom_cookie);
	xcb.delete_window_atom = lavaxcb_get_atom_reply(xcb.connection, "WM_DELETE_WINDOW", delete_window_atom_cookie);
	xcb.state_atom = lavaxcb_get_atom_reply(xcb.connection, "_NET_WM_STATE", state_atom_cookie);
	xcb.state_fullscreen_atom = lavaxcb_get_atom_reply(xcb.connection, "_NET_WM_STATE_FULLSCREEN", state_fullscreen_atom_cookie);
	xcb.bypass_compositor_atom = lavaxcb_get_atom_reply(xcb.connection, "_NET_WM_BYPASS_COMPOSITOR", bypass_compositor_atom_cookie);

	xcb_flush(xcb.connection);
	VkXcbSurfaceCreateInfoKHR pInfo = {};
	pInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	pInfo.flags = 0;
	pInfo.connection = xcb.connection;
	pInfo.window = xcb.window;
	PFN_vkCreateXcbSurfaceKHR ttCreateXcbSurfaceKHR = (PFN_vkCreateXcbSurfaceKHR)vkGetInstanceProcAddr(vulkan.instance, "vkCreateXcbSurfaceKHR");
	assert(ttCreateXcbSurfaceKHR);
	VkResult result = ttCreateXcbSurfaceKHR(vulkan.instance, &pInfo, nullptr, &xcb.surface);
	if (result != VK_SUCCESS)
	{
		ABORT("Failed to create XCB Vulkan surface");
	}

	window_fullscreen(xcb, fullscreen);
	(void)fullscreen;
	return xcb;
#elif USE_HEADLESS
	testwindow ret = {};
	VkHeadlessSurfaceCreateInfoEXT pInfo = { VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT, nullptr };
	pInfo.flags = 0;
	PFN_vkCreateHeadlessSurfaceEXT ttCreateHeadlessSurfaceEXT = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(vkGetInstanceProcAddr(vulkan.instance,"vkCreateHeadlessSurfaceEXT"));
	assert(ttCreateHeadlessSurfaceEXT);
	VkResult result = ttCreateHeadlessSurfaceEXT(vulkan.instance, &pInfo, nullptr, &ret.surface);
	if (result != VK_SUCCESS)
	{
		ABORT("Failed to create headless surface");
	}

	return ret;
#endif
}

void test_window_destroy(const vulkan_setup_t& vulkan, testwindow &w)
{
	window_fullscreen(w, false);
#ifdef USE_XCB
	if (w.window != 0)
	{
		xcb_destroy_window(w.connection, w.window);
	}
	if (w.connection != nullptr)
	{
		xcb_disconnect(w.connection);
	}
#endif
	vkDestroySurfaceKHR(vulkan.instance, w.surface, nullptr);
}
