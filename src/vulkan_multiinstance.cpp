#include "vulkan_common.h"
#include <inttypes.h>

static vulkan_req_t reqs;
static int vulkan_variant = 1;

static void show_usage()
{
	printf("-V/--vulkan-variant N  Set Vulkan variant (default %d)\n", vulkan_variant);
	printf("\t0 - Vulkan 1.0\n");
	printf("\t1 - Vulkan 1.1\n");
	printf("\t2 - Vulkan 1.2\n");
	printf("\t3 - Vulkan 1.3\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-V", "--vulkan-variant"))
	{
		vulkan_variant = get_arg(argv, ++i, argc);
		if (vulkan_variant == 0) reqs.apiVersion = VK_API_VERSION_1_0;
		else if (vulkan_variant == 1) reqs.apiVersion = VK_API_VERSION_1_1;
		else if (vulkan_variant == 2) reqs.apiVersion = VK_API_VERSION_1_2;
		else if (vulkan_variant == 3) reqs.apiVersion = VK_API_VERSION_1_3;
		return (vulkan_variant >= 0 && vulkan_variant <= 3);
	}
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
	if (vulkan_variant >= 2)
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
