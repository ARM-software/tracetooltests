#include "vulkan_common.h"
#include <inttypes.h>

static vulkan_req_t reqs;
static int loops = 250000;
static int variant = 1;

static inline uint64_t mygettime()
{
	struct timespec t;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
	return ((uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec);
}

static void show_usage()
{
	printf("-c/--case N            Choose test case (default %d)\n", variant);
	printf("\t1 - vkEnumeratePhysicalDeviceGroups\n");
	printf("\t2 - vkGetFenceStatus\n");
	printf("-l/--loops N           Number of loops to run (default %d)\n", loops);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-c", "--case"))
	{
		variant = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-l", "--loops"))
	{
		loops = get_arg(argv, ++i, argc);
		return true;
	}
	return false;
}

static const char* case_1(vulkan_setup_t& vulkan, bool active)
{
	VkResult r;
	bench_set_scene(vulkan.bench, "case 1 : vkEnumeratePhysicalDeviceGroups");
	if (active) bench_start_iteration(vulkan.bench);
	for (int i = 0; i < loops; i++)
	{
		uint32_t devgrpcount = 0;
		r = vkEnumeratePhysicalDeviceGroups(vulkan.instance, &devgrpcount, nullptr);
		check(r);
	}
	if (active) bench_stop_iteration(vulkan.bench);
	return "vkEnumeratePhysicalDeviceGroups";
}

static const char* case_2(vulkan_setup_t& vulkan, bool active)
{
	VkResult r;
	VkFence fence;
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(r);
	bench_set_scene(vulkan.bench, "case 2 : vkGetFenceStatus");
	if (active) bench_start_iteration(vulkan.bench);
	for (int i = 0; i < loops; i++)
	{
		r = vkGetFenceStatus(vulkan.device, fence);
	}
	if (active) bench_stop_iteration(vulkan.bench);
	vkDestroyFence(vulkan.device, fence, nullptr);
	return "vkGetFenceStatus";
}

int main(int argc, char** argv)
{
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_stress_1", reqs);

	// warmup
	switch (variant)
	{
	case 1: case_1(vulkan, false); break;
	case 2: case_2(vulkan, false); break;
	default: assert(false);
	}

	// measurement
	const char* name = "no such test case";
	uint64_t before = mygettime();
	switch (variant)
	{
	case 1: name = case_1(vulkan, true); break;
	case 2: name = case_2(vulkan, true); break;
	default: assert(false);
	}
	uint64_t after = mygettime();
	printf("Test case %d - %s, %d iterations: %lu\n", (int)variant, name, (int)loops, (unsigned long)(after - before));

	test_done(vulkan);

	return 0;
}
