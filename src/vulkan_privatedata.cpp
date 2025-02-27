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
	reqs.reqfeat13.privateData = VK_TRUE;
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_privatedata", reqs);
	VkResult r;

	bench_start_iteration(vulkan.bench);

	// Test private data extension

	VkFence fence;
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(r);

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
	vkDestroyFence(vulkan.device, fence, nullptr);

	bench_stop_iteration(vulkan.bench);
	test_done(vulkan);

	return 0;
}
