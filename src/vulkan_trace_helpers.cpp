#include "vulkan_common.h"
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
	reqs.device_extensions.push_back("VK_ARM_trace_helpers");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_trace_helpers", reqs);

	bench_start_iteration(vulkan.bench);

	// no-op, this test is simply meant to fail if this extension is not enabled for now

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
