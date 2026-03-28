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

static void submit_empty(VkQueue queue,
                         uint32_t wait_count,
                         const VkSemaphore* wait_semaphores,
                         const VkPipelineStageFlags* wait_stages,
                         uint32_t signal_count,
                         const VkSemaphore* signal_semaphores,
                         VkFence fence)
{
	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.waitSemaphoreCount = wait_count;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = wait_stages;
	submit_info.commandBufferCount = 0;
	submit_info.pCommandBuffers = nullptr;
	submit_info.signalSemaphoreCount = signal_count;
	submit_info.pSignalSemaphores = signal_semaphores;
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
	reqs.queues = 2;
	reqs.device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_external_semaphore_fd_2", reqs);

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

	VkQueue wait_queue = VK_NULL_HANDLE;
	VkQueue signal_queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &wait_queue);
	vkGetDeviceQueue(vulkan.device, 0, 1, &signal_queue);
	assert(wait_queue != VK_NULL_HANDLE);
	assert(signal_queue != VK_NULL_HANDLE);

	bench_start_iteration(vulkan.bench);

	VkExportSemaphoreCreateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr };
	export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	VkSemaphoreCreateInfo create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &export_info };

	VkSemaphore source_semaphore = VK_NULL_HANDLE;
	VkResult result = vkCreateSemaphore(vulkan.device, &create_info, nullptr, &source_semaphore);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)source_semaphore, "temporary-import-source");

	create_info.pNext = nullptr;

	VkSemaphore destination_semaphore = VK_NULL_HANDLE;
	result = vkCreateSemaphore(vulkan.device, &create_info, nullptr, &destination_semaphore);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)destination_semaphore, "temporary-import-destination");

	VkSemaphoreGetFdInfoKHR get_fd_info = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, nullptr };
	get_fd_info.semaphore = source_semaphore;
	get_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	int exported_fd = -1;
	result = pf_vkGetSemaphoreFdKHR(vulkan.device, &get_fd_info, &exported_fd);
	check(result);
	assert(exported_fd >= 0);

	VkImportSemaphoreFdInfoKHR import_fd_info = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR, nullptr };
	import_fd_info.semaphore = destination_semaphore;
	import_fd_info.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
	import_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	import_fd_info.fd = exported_fd;
	result = pf_vkImportSemaphoreFdKHR(vulkan.device, &import_fd_info);
	check(result);

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	test_marker(vulkan, "first wait must use the temporarily imported payload");
	VkFence first_wait_fence = create_fence(vulkan, "temp-import-first-wait-fence");
	submit_empty(wait_queue, 1, &destination_semaphore, &wait_stage, 0, nullptr, first_wait_fence);
	result = vkWaitForFences(vulkan.device, 1, &first_wait_fence, VK_TRUE, 0);
	assert(result == VK_TIMEOUT);

	test_marker(vulkan, "signal source semaphore to release the imported payload wait");
	submit_empty(signal_queue, 0, nullptr, nullptr, 1, &source_semaphore, VK_NULL_HANDLE);
	result = vkWaitForFences(vulkan.device, 1, &first_wait_fence, VK_TRUE, UINT64_MAX);
	check(result);

	test_marker(vulkan, "second wait must use the restored permanent payload");
	VkFence second_wait_fence = create_fence(vulkan, "temp-import-second-wait-fence");
	submit_empty(wait_queue, 1, &destination_semaphore, &wait_stage, 0, nullptr, second_wait_fence);
	result = vkWaitForFences(vulkan.device, 1, &second_wait_fence, VK_TRUE, 0);
	assert(result == VK_TIMEOUT);

	test_marker(vulkan, "signal destination semaphore to satisfy its restored permanent payload");
	submit_empty(signal_queue, 0, nullptr, nullptr, 1, &destination_semaphore, VK_NULL_HANDLE);
	result = vkWaitForFences(vulkan.device, 1, &second_wait_fence, VK_TRUE, UINT64_MAX);
	check(result);

	vkDestroyFence(vulkan.device, first_wait_fence, nullptr);
	vkDestroyFence(vulkan.device, second_wait_fence, nullptr);
	vkDestroySemaphore(vulkan.device, destination_semaphore, nullptr);
	vkDestroySemaphore(vulkan.device, source_semaphore, nullptr);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
