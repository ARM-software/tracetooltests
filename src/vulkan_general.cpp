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
		assert(r == VK_SUCCESS);
		assert(pApiVersion > 0);
		printf("vkEnumerateInstanceVersion says Vulkan %d.%d.%d)\n", VK_VERSION_MAJOR(pApiVersion),
                       VK_VERSION_MINOR(pApiVersion), VK_VERSION_PATCH(pApiVersion));
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

	if (reqs.apiVersion >= VK_API_VERSION_1_1)
	{
		uint32_t devgrpcount = 0;
		r = vkEnumeratePhysicalDeviceGroups(vulkan.instance, &devgrpcount, nullptr);
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

	// Test private data

	VkFence fence;
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(r);

	bool private_data_support = false;
	if (reqs.apiVersion >= VK_API_VERSION_1_3)
	{
		VkPhysicalDevicePrivateDataFeatures pFeat = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES, nullptr };
		VkPhysicalDeviceFeatures2 feat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &pFeat };
		vkGetPhysicalDeviceFeatures2(vulkan.physical, &feat2);
		private_data_support = pFeat.privateData;
		if (!private_data_support) ILOG("Private data feature not supported!");
	}
	if (reqs.apiVersion >= VK_API_VERSION_1_3 && private_data_support)
	{
		VkPrivateDataSlotCreateInfo pdinfo = { VK_STRUCTURE_TYPE_PRIVATE_DATA_SLOT_CREATE_INFO, nullptr, 0 };
		VkPrivateDataSlot pdslot;
		r = vkCreatePrivateDataSlot(vulkan.device, &pdinfo, nullptr, &pdslot);
		check(r);
		r = vkSetPrivateData(vulkan.device, VK_OBJECT_TYPE_FENCE, (uint64_t)fence, pdslot, 1234);
		check(r);
		uint64_t pData = 0;
		vkGetPrivateData(vulkan.device, VK_OBJECT_TYPE_FENCE, (uint64_t)fence, pdslot, &pData);
		assert(pData == 1234);
		vkDestroyPrivateDataSlot(vulkan.device, pdslot, nullptr);
		// TBD test device pre-allocate private data using VkDevicePrivateDataCreateInfo + VK_STRUCTURE_TYPE_DEVICE_PRIVATE_DATA_CREATE_INFO
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
		vkDestroyFence(vulkan.device, fence, nullptr);
		test_done(vulkan);
	}

	return 0;
}
