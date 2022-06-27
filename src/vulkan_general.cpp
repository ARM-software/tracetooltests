#include "vulkan_common.h"

static int fence_variant = 0;
static bool ugly_exit = false;

static void show_usage()
{
	printf("-x/--ugly-exit         Exit without cleanup\n");
	printf("-f/--fence-variant N   Set fence variant (default %d)\n", fence_variant);
	printf("\t0 - normal run\n");
	printf("\t1 - expect induced fence delay\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-x", "--ugly-exit"))
	{
		ugly_exit = true;
		return true;
	}
	else if (match(argv[i], "-f", "--fence-variant"))
	{
		fence_variant = get_arg(argv, ++i, argc);
		return (fence_variant >= 0 && fence_variant <= 1);
	}
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_general", reqs);
	VkResult r;

	// Test tool interference in function lookups
	PFN_vkVoidFunction badptr = vkGetInstanceProcAddr(nullptr, "vkNonsense");
	assert(!badptr);
	badptr = vkGetInstanceProcAddr(vulkan.instance, "vkNonsense");
	assert(!badptr);
	badptr = vkGetDeviceProcAddr(vulkan.device, "vkNonsense");
	assert(!badptr);
	PFN_vkVoidFunction goodptr = vkGetInstanceProcAddr(nullptr, "vkCreateInstance");
	assert(goodptr);
	goodptr = vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties");
	assert(goodptr);
	goodptr = vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
	assert(goodptr);
	goodptr = vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");
	assert(goodptr);
	goodptr = vkGetInstanceProcAddr(nullptr, "vkGetInstanceProcAddr"); // Valid starting with Vulkan 1.2
	assert(goodptr);
	goodptr = vkGetInstanceProcAddr(vulkan.instance, "vkGetInstanceProcAddr");
	assert(goodptr);

	// Test tool interference in fence handling
	VkFence fence1;
	VkFence fence2;
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence1);
	check(r);
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence2);
	check(r);

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	r = vkQueueSubmit(queue, 0, nullptr, fence1); // easiest way to signal a fence...
	check(r);

	r = vkWaitForFences(vulkan.device, 1, &fence1, VK_TRUE, UINT32_MAX - 1);
	if (fence_variant == 0) assert(r == VK_SUCCESS);
	else assert(r == VK_TIMEOUT);

	std::vector<VkFence> fences = { fence1, fence2 }; // one signaled, one unsignaled
	r = vkWaitForFences(vulkan.device, 2, fences.data(), VK_TRUE, 10);
	assert(r == VK_TIMEOUT);

	r = vkGetFenceStatus(vulkan.device, fence1);
	if (fence_variant == 0) assert(r == VK_SUCCESS);
	else assert(r == VK_NOT_READY);

	r = vkGetFenceStatus(vulkan.device, fence2);
	assert(r == VK_NOT_READY);

	r = vkResetFences(vulkan.device, 2, fences.data());
	check(r);
	r = vkGetFenceStatus(vulkan.device, fence1);
	assert(r == VK_NOT_READY);

	if (!ugly_exit)
	{
		vkDestroyFence(vulkan.device, fence1, nullptr);
		vkDestroyFence(vulkan.device, fence2, nullptr);
		test_done(vulkan);
	}

	return 0;
}
