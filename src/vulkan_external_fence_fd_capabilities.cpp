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

static VkExternalFencePropertiesKHR get_external_fence_properties(const vulkan_setup_t& vulkan,
                                                                 PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR vkGetPhysicalDeviceExternalFencePropertiesKHR)
{
	VkPhysicalDeviceExternalFenceInfoKHR external_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO_KHR, nullptr
	};
	external_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

	VkExternalFencePropertiesKHR external_properties = {
		VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES_KHR, nullptr
	};
	vkGetPhysicalDeviceExternalFencePropertiesKHR(vulkan.physical, &external_info, &external_properties);

	return external_properties;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.minApiVersion = VK_API_VERSION_1_0;
	reqs.apiVersion = VK_API_VERSION_1_0;
	reqs.instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	reqs.instance_extensions.push_back(VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_external_fence_fd_capabilities", reqs);

	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceExternalFencePropertiesKHR);
	MAKEDEVICEPROCADDR(vulkan, vkGetFenceFdKHR);
	MAKEDEVICEPROCADDR(vulkan, vkImportFenceFdKHR);

	VkExternalFencePropertiesKHR external_properties =
		get_external_fence_properties(vulkan, pf_vkGetPhysicalDeviceExternalFencePropertiesKHR);

	const VkExternalFenceFeatureFlagsKHR required_features =
		VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT_KHR |
		VK_EXTERNAL_FENCE_FEATURE_IMPORTABLE_BIT_KHR;
	if ((external_properties.externalFenceFeatures & required_features) != required_features)
	{
		printf("Skipping: OPAQUE_FD external fence import/export not supported on this device\n");
		test_done(vulkan);
		return 77;
	}

	assert((external_properties.compatibleHandleTypes & VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR) != 0);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	bench_start_iteration(vulkan.bench);

	VkFence source = create_fence(vulkan, "capabilities-source-fence", true);
	VkFence destination = create_fence(vulkan, "capabilities-destination-fence", false);
	test_marker_mention(vulkan,
	                    "Import OPAQUE_FD fence payload after querying external fence capabilities",
	                    VK_OBJECT_TYPE_FENCE,
	                    (uint64_t)destination);

	int fd = export_fence_fd(vulkan, pf_vkGetFenceFdKHR, source);
	import_fence_fd(vulkan, pf_vkImportFenceFdKHR, destination, fd, 0);

	VkResult result = vkGetFenceStatus(vulkan.device, destination);
	assert(result == VK_NOT_READY);

	test_marker(vulkan, "Signal exported fence payload and wait through the imported fence");
	submit_empty(queue, source);

	result = vkWaitForFences(vulkan.device, 1, &destination, VK_TRUE, UINT64_MAX);
	check(result);
	result = vkGetFenceStatus(vulkan.device, source);
	check(result);
	result = vkGetFenceStatus(vulkan.device, destination);
	check(result);

	vkDestroyFence(vulkan.device, destination, nullptr);
	vkDestroyFence(vulkan.device, source, nullptr);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
