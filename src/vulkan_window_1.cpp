#include "vulkan_window_common.h"
#include <inttypes.h>

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
	reqs.instance_extensions.push_back("VK_KHR_xcb_surface");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_window_1", reqs);

	testwindow w1 = test_window_create(vulkan, 0, 0, 400, 300);
	testwindow w2 = test_window_create(vulkan, 200, 100, 400, 300);

	test_window_destroy(vulkan, w1);
	test_window_destroy(vulkan, w2);

	test_done(vulkan);

	return 0;
}
