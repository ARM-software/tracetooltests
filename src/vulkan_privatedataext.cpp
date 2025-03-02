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
	VkPhysicalDevicePrivateDataFeaturesEXT privfeats = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES_EXT, nullptr, VK_TRUE };
	reqs.device_extensions.push_back("VK_EXT_private_data");
	reqs.extension_features = (VkBaseInStructure*)&privfeats;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.maxApiVersion = VK_API_VERSION_1_2;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_privatedataext", reqs);
	VkResult r;

	bench_start_iteration(vulkan.bench);

	// Test private data extension

	VkFence fence;
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(r);

	MAKEDEVICEPROCADDR(vulkan, vkCreatePrivateDataSlotEXT);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyPrivateDataSlotEXT);
	MAKEDEVICEPROCADDR(vulkan, vkSetPrivateDataEXT);
	MAKEDEVICEPROCADDR(vulkan, vkGetPrivateDataEXT);

	VkPrivateDataSlotCreateInfoEXT pdinfo = { VK_STRUCTURE_TYPE_PRIVATE_DATA_SLOT_CREATE_INFO_EXT, nullptr, 0 };
	VkPrivateDataSlotEXT pdslot;
	r = pf_vkCreatePrivateDataSlotEXT(vulkan.device, &pdinfo, nullptr, &pdslot);
	check(r);
	r = pf_vkSetPrivateDataEXT(vulkan.device, VK_OBJECT_TYPE_FENCE, (uint64_t)fence, pdslot, 1234);
	check(r);
	uint64_t pData = 0;
	pf_vkGetPrivateDataEXT(vulkan.device, VK_OBJECT_TYPE_FENCE, (uint64_t)fence, pdslot, &pData);
	assert(pData == 1234);
	pf_vkDestroyPrivateDataSlotEXT(vulkan.device, pdslot, nullptr);
	vkDestroyFence(vulkan.device, fence, nullptr);

	test_done(vulkan);

	return 0;
}
