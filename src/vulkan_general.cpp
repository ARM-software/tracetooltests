#include "vulkan_common.h"
#include <inttypes.h>

static bool ugly_exit = false;
static vulkan_req_t reqs;

static void show_usage()
{
	printf("-x/--ugly-exit         		Exit without cleanup\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-x", "--ugly-exit"))
	{
		ugly_exit = true;
		return true;
	}
	return false;
}

int main(int argc, char** argv)
{
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_general", reqs);
	VkResult r;

	bench_start_iteration(vulkan.bench);

	// Test vkEnumerateInstanceVersion

	if (reqs.apiVersion >= VK_API_VERSION_1_1)
	{
		uint32_t pApiVersion = 0;
		r = vkEnumerateInstanceVersion(&pApiVersion); // new in Vulkan 1.1
		check(r);
		assert(pApiVersion > 0);
		printf("vkEnumerateInstanceVersion says Vulkan %d.%d.%d)\n", VK_VERSION_MAJOR(pApiVersion),
                       VK_VERSION_MINOR(pApiVersion), VK_VERSION_PATCH(pApiVersion));
	}

	// Test VkPhysicalDeviceDriverProperties

	if (reqs.apiVersion >= VK_API_VERSION_1_2)
	{
		VkPhysicalDeviceDriverProperties driverprops = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES, nullptr };
		VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driverprops };
		vkGetPhysicalDeviceProperties2(vulkan.physical, &props);
		printf("Driver name: %s\n", driverprops.driverName);
		printf("Driver info: %s\n", driverprops.driverInfo);
	}

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
	if (reqs.apiVersion >= VK_API_VERSION_1_2)
	{
		goodptr = vkGetInstanceProcAddr(nullptr, "vkGetInstanceProcAddr"); // Valid starting with Vulkan 1.2
		assert(goodptr);
	}
	goodptr = vkGetInstanceProcAddr(vulkan.instance, "vkGetInstanceProcAddr");
	assert(goodptr);

	// silence silly compilers
	(void)badptr;
	(void)goodptr;

	if (reqs.apiVersion >= VK_API_VERSION_1_1)
	{
		uint32_t devgrpcount = 0;
		r = vkEnumeratePhysicalDeviceGroups(vulkan.instance, &devgrpcount, nullptr);
		check(r);
		std::vector<VkPhysicalDeviceGroupProperties> devgrps(devgrpcount);
		for (auto& v : devgrps) { v.pNext = nullptr; v.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES; }
		printf("Found %u physical device groups:\n", devgrpcount);
		r = vkEnumeratePhysicalDeviceGroups(vulkan.instance, &devgrpcount, devgrps.data());
		for (auto& v : devgrps)
		{
			printf("\t%u devices (subsetAllocation=%s):", v.physicalDeviceCount, v.subsetAllocation ? "true" : "false");
			for (unsigned i = 0; i < v.physicalDeviceCount; i++) printf(" 0x%" PRIx64 ",", (uint64_t)v.physicalDevices[i]);
			printf("\n");
		}
	}

	// Test valid null destroy commands

	vkDestroyInstance(VK_NULL_HANDLE, nullptr);
	vkDestroyDevice(VK_NULL_HANDLE, nullptr);
	vkDestroyFence(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroySemaphore(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyEvent(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyQueryPool(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyBuffer(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyBufferView(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyImage(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyImageView(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyShaderModule(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyPipelineCache(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyPipeline(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyPipelineLayout(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroySampler(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyDescriptorPool(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyFramebuffer(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyRenderPass(vulkan.device, VK_NULL_HANDLE, nullptr);
	vkDestroyCommandPool(vulkan.device, VK_NULL_HANDLE, nullptr);
	if (reqs.apiVersion >= VK_API_VERSION_1_1)
	{
		vkDestroySamplerYcbcrConversion(vulkan.device, VK_NULL_HANDLE, nullptr);
		vkDestroyDescriptorUpdateTemplate(vulkan.device, VK_NULL_HANDLE, nullptr);
	}
	if (reqs.apiVersion >= VK_API_VERSION_1_3)
	{
		vkDestroyPrivateDataSlot(vulkan.device, VK_NULL_HANDLE, nullptr);
	}

	bench_stop_iteration(vulkan.bench);

	// Optionally test ugly exit

	if (!ugly_exit)
	{
		test_done(vulkan);
	}

	return 0;
}
