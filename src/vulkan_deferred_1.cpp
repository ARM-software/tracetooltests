#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.device_extensions.push_back("VK_KHR_deferred_host_operations");
	reqs.apiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_deferred_1", reqs);

	auto ttCreateDeferredOperationKHR = (PFN_vkCreateDeferredOperationKHR)vkGetDeviceProcAddr(vulkan.device, "vkCreateDeferredOperationKHR");
	assert(ttCreateDeferredOperationKHR);
	auto ttDestroyDeferredOperationKHR = (PFN_vkDestroyDeferredOperationKHR)vkGetDeviceProcAddr(vulkan.device, "vkDestroyDeferredOperationKHR");
	assert(ttDestroyDeferredOperationKHR);
	auto ttGetDeferredOperationResultKHR = (PFN_vkGetDeferredOperationResultKHR)vkGetDeviceProcAddr(vulkan.device, "vkGetDeferredOperationResultKHR");
	assert(ttGetDeferredOperationResultKHR);
	auto ttGetDeferredOperationMaxConcurrencyKHR = (PFN_vkGetDeferredOperationMaxConcurrencyKHR)vkGetDeviceProcAddr(vulkan.device, "vkGetDeferredOperationMaxConcurrencyKHR");
	assert(ttGetDeferredOperationMaxConcurrencyKHR);
	auto ttDeferredOperationJoinKHR = (PFN_vkDeferredOperationJoinKHR)vkGetDeviceProcAddr(vulkan.device, "vkDeferredOperationJoinKHR");
	assert(ttDeferredOperationJoinKHR);

	VkDeferredOperationKHR hOp;
	VkResult result = ttCreateDeferredOperationKHR(vulkan.device, nullptr, &hOp);
	assert(result == VK_SUCCESS);

	result = ttDeferredOperationJoinKHR(vulkan.device, hOp);
	assert(result == VK_SUCCESS || result == VK_THREAD_DONE_KHR || result == VK_THREAD_IDLE_KHR);

	result = ttGetDeferredOperationResultKHR(vulkan.device, hOp);
	assert(result == VK_SUCCESS);

	uint32_t r = ttGetDeferredOperationMaxConcurrencyKHR(vulkan.device, hOp);
	assert(r == 0 || r == UINT32_MAX - 1);

	ttDestroyDeferredOperationKHR(vulkan.device, hOp, nullptr);

	test_done(vulkan);
	return 0;
}
