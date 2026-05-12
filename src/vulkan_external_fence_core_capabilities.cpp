#include "vulkan_common.h"

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static VkExternalFenceProperties get_external_fence_properties(const vulkan_setup_t& vulkan, VkExternalFenceHandleTypeFlagBits handle_type)
{
	VkPhysicalDeviceExternalFenceInfo external_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO, nullptr
	};
	external_info.handleType = handle_type;

	VkExternalFenceProperties external_properties = {
		VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES, nullptr
	};
	vkGetPhysicalDeviceExternalFenceProperties(vulkan.physical, &external_info, &external_properties);

	assert(external_properties.sType == VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES);
	assert(external_properties.pNext == nullptr);
	return external_properties;
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

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_external_fence_core_capabilities", reqs);

	bench_start_iteration(vulkan.bench);

	VkExternalFenceProperties opaque_fd_properties =
		get_external_fence_properties(vulkan, VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT);
	VkExternalFenceProperties sync_fd_properties =
		get_external_fence_properties(vulkan, VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT);

	test_marker(vulkan, "Queried Vulkan 1.1 external fence capabilities");

	assert((opaque_fd_properties.compatibleHandleTypes & VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT) != 0 ||
	       opaque_fd_properties.externalFenceFeatures == 0);
	assert((sync_fd_properties.compatibleHandleTypes & VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT) != 0 ||
	       sync_fd_properties.externalFenceFeatures == 0);

	const bool opaque_fd_exportable =
		(opaque_fd_properties.externalFenceFeatures & VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT) != 0;
	if (opaque_fd_exportable)
	{
		VkQueue queue = VK_NULL_HANDLE;
		vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
		assert(queue != VK_NULL_HANDLE);

		VkFence fence = create_fence(vulkan, "core-capabilities-exportable-fence", true);
		test_marker_mention(vulkan,
		                    "Created exportable fence using Vulkan 1.1 core external fence structures",
		                    VK_OBJECT_TYPE_FENCE,
		                    (uint64_t)fence);

		submit_empty(queue, fence);

		VkResult result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
		check(result);
		result = vkGetFenceStatus(vulkan.device, fence);
		check(result);

		vkDestroyFence(vulkan.device, fence, nullptr);
	}

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
