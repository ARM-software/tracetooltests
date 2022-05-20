#include "vulkan_common.h"

static int fence_variant = 0;

void usage()
{
	printf("Usage: vulkan_general\n");
	printf("-h/--help              This help\n");
	printf("-g/--gpu level N       Select GPU (default 0)\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default %d)\n", p__debug_level);
	printf("-f/--fence-variant N   Set fence variant (default %d)\n", fence_variant);
	printf("\t0 - normal run\n");
	printf("\t1 - expect induced fence delay\n");
	exit(0);
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++)
	{
		if (match(argv[i], "-h", "--help"))
		{
			usage();
		}
		else if (match(argv[i], "-d", "--debug"))
		{
			p__debug_level = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-g", "--gpu"))
		{
			select_gpu(get_arg(argv, ++i, argc));
		}
		else if (match(argv[i], "-f", "--fence-variant"))
		{
			fence_variant = get_arg(argv, ++i, argc);
			if (fence_variant < 0 || fence_variant > 1)
			{
				usage();
			}
		}
		else
		{
			ELOG("Unrecognized cmd line parameter: %s", argv[i]);
			return -1;
		}
	}

	vulkan_setup_t vulkan = test_init("vulkan_general");
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

	vkDestroyFence(vulkan.device, fence1, nullptr);
	vkDestroyFence(vulkan.device, fence2, nullptr);

	test_done(vulkan);
	return 0;
}
