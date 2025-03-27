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
	vulkan_setup_t vulkan1 = test_init(argc, argv, "vulkan_multidevice", reqs);
	reqs.instance = vulkan1.instance;
	vulkan_setup_t vulkan2 = test_init(argc, argv, "vulkan_multidevice", reqs);

	ILOG("Created device1 with physical=%lu and device2 with physical=%lu", (unsigned long)vulkan1.physical, (unsigned long)vulkan2.physical);

	VkPhysicalDeviceFeatures features = {};
	vkGetPhysicalDeviceFeatures(vulkan2.physical, &features);
	void* ptr = (void*)vkGetDeviceProcAddr(vulkan2.device, "vkDestroyDevice");
	assert(ptr);

	test_done(vulkan2, true);

	vkGetPhysicalDeviceFeatures(vulkan1.physical, &features);
	ptr = (void*)vkGetInstanceProcAddr(vulkan1.instance, "vkGetPhysicalDeviceFeatures");
	assert(ptr);
	ptr = (void*)vkGetDeviceProcAddr(vulkan1.device, "vkDestroyDevice");
	assert(ptr);

	test_done(vulkan1);

	return 0;
}
