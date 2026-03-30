#include "vulkan_common.h"

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static VkFence create_fence(const vulkan_setup_t& vulkan, const char* name, bool exportable)
{
	VkExportFenceCreateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO, nullptr };
	export_info.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;

	VkFenceCreateInfo create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, exportable ? &export_info : nullptr };

	VkFence fence = VK_NULL_HANDLE;
	VkResult result = vkCreateFence(vulkan.device, &create_info, nullptr, &fence);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_FENCE, (uint64_t)fence, name);
	return fence;
}

static void submit_empty(VkQueue queue, VkFence fence)
{
	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 0;
	submit_info.pCommandBuffers = nullptr;
	submit_info.waitSemaphoreCount = 0;
	submit_info.pWaitSemaphores = nullptr;
	submit_info.pWaitDstStageMask = nullptr;
	submit_info.signalSemaphoreCount = 0;
	submit_info.pSignalSemaphores = nullptr;
	VkResult result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
}

static int export_fence_fd(const vulkan_setup_t& vulkan, PFN_vkGetFenceFdKHR vkGetFenceFdKHR, VkFence fence)
{
	VkFenceGetFdInfoKHR get_fd_info = { VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR, nullptr };
	get_fd_info.fence = fence;
	get_fd_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;

	int exported_fd = -1;
	VkResult result = vkGetFenceFdKHR(vulkan.device, &get_fd_info, &exported_fd);
	check(result);
	assert(exported_fd >= 0);
	return exported_fd;
}

static void import_fence_fd(const vulkan_setup_t& vulkan,
                            PFN_vkImportFenceFdKHR vkImportFenceFdKHR,
                            VkFence fence,
                            int fd,
                            VkFenceImportFlags flags)
{
	VkImportFenceFdInfoKHR import_fd_info = { VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR, nullptr };
	import_fd_info.fence = fence;
	import_fd_info.flags = flags;
	import_fd_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	import_fd_info.fd = fd;

	VkResult result = vkImportFenceFdKHR(vulkan.device, &import_fd_info);
	check(result);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_external_fence_fd", reqs);

	VkPhysicalDeviceExternalFenceInfo external_info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO, nullptr };
	external_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;

	VkExternalFenceProperties external_properties = { VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES, nullptr };
	vkGetPhysicalDeviceExternalFenceProperties(vulkan.physical, &external_info, &external_properties);

	const VkExternalFenceFeatureFlags required_features =
		VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT |
		VK_EXTERNAL_FENCE_FEATURE_IMPORTABLE_BIT;
	if ((external_properties.externalFenceFeatures & required_features) != required_features)
	{
		printf("Skipping: OPAQUE_FD external fence import/export not supported on this device\n");
		test_done(vulkan);
		return 77;
	}

	MAKEDEVICEPROCADDR(vulkan, vkGetFenceFdKHR);
	MAKEDEVICEPROCADDR(vulkan, vkImportFenceFdKHR);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	bench_start_iteration(vulkan.bench);

	VkFence permanent_source = create_fence(vulkan, "permanent-source-fence", true);
	VkFence permanent_destination = create_fence(vulkan, "permanent-destination-fence", false);
	test_marker_mention(vulkan,
	                    "Permanently import OPAQUE_FD fence payload",
	                    VK_OBJECT_TYPE_FENCE,
	                    (uint64_t)permanent_destination);

	int permanent_fd = export_fence_fd(vulkan, pf_vkGetFenceFdKHR, permanent_source);
	import_fence_fd(vulkan, pf_vkImportFenceFdKHR, permanent_destination, permanent_fd, 0);

	VkResult result = vkGetFenceStatus(vulkan.device, permanent_destination);
	assert(result == VK_NOT_READY);

	test_marker(vulkan, "Signal source fence and observe the imported payload from the destination fence");
	submit_empty(queue, permanent_source);

	result = vkWaitForFences(vulkan.device, 1, &permanent_destination, VK_TRUE, UINT64_MAX);
	check(result);
	result = vkGetFenceStatus(vulkan.device, permanent_source);
	check(result);
	result = vkGetFenceStatus(vulkan.device, permanent_destination);
	check(result);

	VkFence temporary_source = create_fence(vulkan, "temporary-source-fence", true);
	VkFence temporary_destination = create_fence(vulkan, "temporary-destination-fence", false);
	test_marker_mention(vulkan,
	                    "Temporarily import OPAQUE_FD fence payload",
	                    VK_OBJECT_TYPE_FENCE,
	                    (uint64_t)temporary_destination);

	int temporary_fd = export_fence_fd(vulkan, pf_vkGetFenceFdKHR, temporary_source);
	import_fence_fd(vulkan,
	                pf_vkImportFenceFdKHR,
	                temporary_destination,
	                temporary_fd,
	                VK_FENCE_IMPORT_TEMPORARY_BIT);

	result = vkGetFenceStatus(vulkan.device, temporary_destination);
	assert(result == VK_NOT_READY);

	test_marker(vulkan, "Temporary import must signal from the source fence first");
	submit_empty(queue, temporary_source);

	result = vkWaitForFences(vulkan.device, 1, &temporary_destination, VK_TRUE, UINT64_MAX);
	check(result);

	test_marker(vulkan, "Resetting a temporarily imported fence must restore its permanent payload");
	result = vkResetFences(vulkan.device, 1, &temporary_destination);
	check(result);
	result = vkGetFenceStatus(vulkan.device, temporary_destination);
	assert(result == VK_NOT_READY);

	submit_empty(queue, temporary_destination);
	result = vkWaitForFences(vulkan.device, 1, &temporary_destination, VK_TRUE, UINT64_MAX);
	check(result);

	vkDestroyFence(vulkan.device, temporary_destination, nullptr);
	vkDestroyFence(vulkan.device, temporary_source, nullptr);
	vkDestroyFence(vulkan.device, permanent_destination, nullptr);
	vkDestroyFence(vulkan.device, permanent_source, nullptr);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
