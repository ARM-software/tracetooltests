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
	vulkan_setup_t vulkan1 = test_init(argc, argv, "vulkan_multiinstance_1", reqs);
	vulkan_setup_t vulkan2 = test_init(argc, argv, "vulkan_multiinstance_2", reqs);

	// Test function lookups

	PFN_vkVoidFunction badptr = vkGetInstanceProcAddr(nullptr, "vkNonsense");
	assert(!badptr);
	badptr = vkGetInstanceProcAddr(vulkan1.instance, "vkNonsense");
	assert(!badptr);
	badptr = vkGetDeviceProcAddr(vulkan2.device, "vkNonsense");
	assert(!badptr);
	PFN_vkVoidFunction goodptr = vkGetInstanceProcAddr(nullptr, "vkCreateInstance");
	assert(goodptr);
	goodptr = vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties");
	assert(goodptr);
	goodptr = vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
	assert(goodptr);
	goodptr = vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");
	assert(goodptr);
	if (reqs.apiVersion >= VK_API_VERSION_1_2)
	{
		goodptr = vkGetInstanceProcAddr(nullptr, "vkGetInstanceProcAddr"); // Valid starting with Vulkan 1.2
		assert(goodptr);
	}
	goodptr = vkGetInstanceProcAddr(vulkan1.instance, "vkGetInstanceProcAddr");
	assert(goodptr);

	test_done(vulkan2);
	test_done(vulkan1);

	return 0;
}
