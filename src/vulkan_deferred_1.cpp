#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.device_extensions.push_back("VK_KHR_deferred_host_operations");
	reqs.apiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_deferred_1", reqs);

	MAKEDEVICEPROCADDR(vulkan, vkCreateDeferredOperationKHR);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyDeferredOperationKHR);
	MAKEDEVICEPROCADDR(vulkan, vkGetDeferredOperationResultKHR);
	MAKEDEVICEPROCADDR(vulkan, vkGetDeferredOperationMaxConcurrencyKHR);
	MAKEDEVICEPROCADDR(vulkan, vkDeferredOperationJoinKHR);

	VkDeferredOperationKHR hOp;
	VkResult result = pf_vkCreateDeferredOperationKHR(vulkan.device, nullptr, &hOp);
	assert(result == VK_SUCCESS);

	uint32_t r = pf_vkGetDeferredOperationMaxConcurrencyKHR(vulkan.device, hOp);
	printf("vkGetDeferredOperationMaxConcurrencyKHR returns %u before join\n", (unsigned)r);

	result = pf_vkDeferredOperationJoinKHR(vulkan.device, hOp);
	assert(result == VK_SUCCESS || result == VK_THREAD_DONE_KHR || result == VK_THREAD_IDLE_KHR);

	r = pf_vkGetDeferredOperationMaxConcurrencyKHR(vulkan.device, hOp);
	printf("vkGetDeferredOperationMaxConcurrencyKHR returns %u after join\n", (unsigned)r);

	result = pf_vkGetDeferredOperationResultKHR(vulkan.device, hOp);
	assert(result == VK_SUCCESS);

	r = pf_vkGetDeferredOperationMaxConcurrencyKHR(vulkan.device, hOp);
	printf("vkGetDeferredOperationMaxConcurrencyKHR returns %u after get result\n", (unsigned)r);

	pf_vkDestroyDeferredOperationKHR(vulkan.device, hOp, nullptr);

	test_done(vulkan);
	return 0;
}
