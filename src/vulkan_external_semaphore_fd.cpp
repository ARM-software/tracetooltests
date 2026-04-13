#include "vulkan_common.h"

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static VkFence create_fence(const vulkan_setup_t& vulkan, const char* name)
{
	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkResult result = vkCreateFence(vulkan.device, &create_info, nullptr, &fence);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_FENCE, (uint64_t)fence, name);
	return fence;
}

static void submit_timeline_wait(VkQueue queue, VkSemaphore semaphore, uint64_t wait_value, VkFence fence)
{
	VkTimelineSemaphoreSubmitInfo timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr };
	timeline_info.waitSemaphoreValueCount = 1;
	timeline_info.pWaitSemaphoreValues = &wait_value;
	timeline_info.signalSemaphoreValueCount = 0;
	timeline_info.pSignalSemaphoreValues = nullptr;

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_info };
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &semaphore;
	submit_info.pWaitDstStageMask = &wait_stage;
	submit_info.commandBufferCount = 0;
	submit_info.pCommandBuffers = nullptr;
	submit_info.signalSemaphoreCount = 0;
	submit_info.pSignalSemaphores = nullptr;

	VkResult result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
}

static void signal_timeline(const vulkan_setup_t& vulkan, VkSemaphore semaphore, uint64_t value)
{
	VkSemaphoreSignalInfo signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr };
	signal_info.semaphore = semaphore;
	signal_info.value = value;
	VkResult result = vkSignalSemaphore(vulkan.device, &signal_info);
	check(result);
}

static uint64_t get_timeline_value(const vulkan_setup_t& vulkan, VkSemaphore semaphore)
{
	uint64_t value = 0;
	VkResult result = vkGetSemaphoreCounterValue(vulkan.device, semaphore, &value);
	check(result);
	return value;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.reqfeat12.timelineSemaphore = VK_TRUE;
	reqs.device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_external_semaphore_fd", reqs);

	VkPhysicalDeviceExternalSemaphoreInfo external_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO, nullptr
	};
	external_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

	VkExternalSemaphoreProperties external_properties = {
		VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES, nullptr
	};
	vkGetPhysicalDeviceExternalSemaphoreProperties(vulkan.physical, &external_info, &external_properties);

	const VkExternalSemaphoreFeatureFlags required_features =
		VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
		VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
	if ((external_properties.externalSemaphoreFeatures & required_features) != required_features)
	{
		printf("Skipping: OPAQUE_FD external semaphore import/export not supported on this device\n");
		test_done(vulkan);
		return 77;
	}

	MAKEDEVICEPROCADDR(vulkan, vkGetSemaphoreFdKHR);
	MAKEDEVICEPROCADDR(vulkan, vkImportSemaphoreFdKHR);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	bench_start_iteration(vulkan.bench);

	VkSemaphoreTypeCreateInfo timeline_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr };
	timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	timeline_info.initialValue = 0;

	VkExportSemaphoreCreateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, &timeline_info };
	export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

	VkSemaphoreCreateInfo create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &export_info };

	VkSemaphore exported_semaphore = VK_NULL_HANDLE;
	VkResult result = vkCreateSemaphore(vulkan.device, &create_info, nullptr, &exported_semaphore);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)exported_semaphore, "exported-timeline-semaphore");

	create_info.pNext = &timeline_info;

	VkSemaphore imported_semaphore = VK_NULL_HANDLE;
	result = vkCreateSemaphore(vulkan.device, &create_info, nullptr, &imported_semaphore);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)imported_semaphore, "imported-timeline-semaphore");

	VkSemaphoreGetFdInfoKHR get_fd_info = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, nullptr };
	get_fd_info.semaphore = exported_semaphore;
	get_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	int exported_fd = -1;
	result = pf_vkGetSemaphoreFdKHR(vulkan.device, &get_fd_info, &exported_fd);
	check(result);
	assert(exported_fd >= 0);

	VkImportSemaphoreFdInfoKHR import_fd_info = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR, nullptr };
	import_fd_info.semaphore = imported_semaphore;
	import_fd_info.flags = 0;
	import_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	import_fd_info.fd = exported_fd;
	result = pf_vkImportSemaphoreFdKHR(vulkan.device, &import_fd_info);
	check(result);

	test_marker(vulkan, "queue wait on permanently imported timeline semaphore must block until source payload advances");
	VkFence first_wait_fence = create_fence(vulkan, "first-wait-fence");
	submit_timeline_wait(queue, imported_semaphore, 1, first_wait_fence);
	result = vkWaitForFences(vulkan.device, 1, &first_wait_fence, VK_TRUE, 0);
	assert(result == VK_TIMEOUT);

	test_marker(vulkan, "host signal exported timeline semaphore to satisfy imported wait");
	signal_timeline(vulkan, exported_semaphore, 1);
	result = vkWaitForFences(vulkan.device, 1, &first_wait_fence, VK_TRUE, UINT64_MAX);
	check(result);
	uint64_t r64 = get_timeline_value(vulkan, imported_semaphore);
	assert(r64 == 1);

	test_marker(vulkan, "second imported wait must continue to track the shared payload");
	VkFence second_wait_fence = create_fence(vulkan, "second-wait-fence");
	submit_timeline_wait(queue, imported_semaphore, 2, second_wait_fence);
	result = vkWaitForFences(vulkan.device, 1, &second_wait_fence, VK_TRUE, 0);
	assert(result == VK_TIMEOUT);

	test_marker(vulkan, "host signal imported timeline semaphore to advance the shared payload again");
	signal_timeline(vulkan, imported_semaphore, 2);
	result = vkWaitForFences(vulkan.device, 1, &second_wait_fence, VK_TRUE, UINT64_MAX);
	check(result);
	r64 = get_timeline_value(vulkan, exported_semaphore);
	assert(r64 == 2);

	vkDestroyFence(vulkan.device, first_wait_fence, nullptr);
	vkDestroyFence(vulkan.device, second_wait_fence, nullptr);
	vkDestroySemaphore(vulkan.device, imported_semaphore, nullptr);
	vkDestroySemaphore(vulkan.device, exported_semaphore, nullptr);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
