#include "vulkan_common.h"

static int queue_count = 2;

static void show_usage() { printf("-q / --queue-count N    Use 2 or 3 source queues (default 2)\n"); }

static bool test_cmdopt(int &i, int argc, char **argv, vulkan_req_t &reqs)
{
	if (match(argv[i], "-q", "--queue-count"))
	{
		queue_count = get_arg(argv, ++i, argc);

		if (queue_count != 2 && queue_count != 3)
		{
			printf("Queue count must be 2 or 3\n");
			return false;
		}

		reqs.queues = queue_count;
		return true;
	}

	return false;
}
static bool timeline_semaphore_supported(VkPhysicalDevice physical_device)
{
	VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, nullptr};

	VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features12};

	vkGetPhysicalDeviceFeatures2(physical_device, &features);

	if (!features12.timelineSemaphore)
	{
		printf("Timeline semaphores are not supported\n");
		return false;
	}

	return true;
}

static VkSemaphore create_timeline_semaphore(VkDevice device)
{
	VkSemaphoreTypeCreateInfo type_info = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr};
	type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	type_info.initialValue = 0;

	VkSemaphoreCreateInfo create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type_info};

	VkSemaphore semaphore = VK_NULL_HANDLE;
	VkResult result = vkCreateSemaphore(device, &create_info, nullptr, &semaphore);
	check(result);
	return semaphore;
}

static VkFence create_fence(VkDevice device)
{
	VkFenceCreateInfo create_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr};

	VkFence fence = VK_NULL_HANDLE;
	VkResult result = vkCreateFence(device, &create_info, nullptr, &fence);
	check(result);
	return fence;
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.reqfeat12.timelineSemaphore = VK_TRUE;
	reqs.physical_device_supported = timeline_semaphore_supported;
	reqs.queues = queue_count;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_queue_remap_timeline", reqs);
	const uint32_t runtime_queue_count = reqs.queues;
	if (runtime_queue_count != 2 && runtime_queue_count != 3)
	{
		printf("Queue count must be 2 or 3, got %u\n", runtime_queue_count);
		test_done(vulkan);
		return 77;
	}
	bench_start_iteration(vulkan.bench);

	std::vector<VkQueue> queues(runtime_queue_count, VK_NULL_HANDLE);

	for (uint32_t i = 0; i < runtime_queue_count; i++)
	{
		vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, i, &queues[i]);
		assert(queues[i] != VK_NULL_HANDLE);
		for (uint32_t j = 0; j < i; j++)
			assert(queues[i] != queues[j]);
	}

	// For 2 -> 1, queues 0 and 1 collide. For a modulo 3 -> 2
	// remap, queues 0 and 2 collide.
	VkQueue wait_queue = queues[0];
	VkQueue signal_queue = queues[runtime_queue_count - 1];
	assert(wait_queue != signal_queue);

	VkSemaphore timeline = create_timeline_semaphore(vulkan.device);
	assert(timeline != VK_NULL_HANDLE);

	VkFence fence = create_fence(vulkan.device);
	assert(fence != VK_NULL_HANDLE);

	uint64_t wait_value = 1;
	VkTimelineSemaphoreSubmitInfo wait_timeline_info = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr};
	wait_timeline_info.waitSemaphoreValueCount = 1;
	wait_timeline_info.pWaitSemaphoreValues = &wait_value;

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	VkSubmitInfo wait_submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, &wait_timeline_info};
	wait_submit.waitSemaphoreCount = 1;
	wait_submit.pWaitSemaphores = &timeline;
	wait_submit.pWaitDstStageMask = &wait_stage;

	VkResult result = vkQueueSubmit(wait_queue, 1, &wait_submit, fence);
	check(result);

	uint64_t signal_value = 1;
	VkTimelineSemaphoreSubmitInfo signal_timeline_info = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr};
	signal_timeline_info.signalSemaphoreValueCount = 1;
	signal_timeline_info.pSignalSemaphoreValues = &signal_value;

	VkSubmitInfo signal_submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, &signal_timeline_info};
	signal_submit.signalSemaphoreCount = 1;
	signal_submit.pSignalSemaphores = &timeline;

	result = vkQueueSubmit(signal_queue, 1, &signal_submit, VK_NULL_HANDLE);
	check(result);

	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	uint64_t counter = 0;
	result = vkGetSemaphoreCounterValue(vulkan.device, timeline, &counter);
	check(result);

	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		assert(counter == 1);
	}

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkDestroySemaphore(vulkan.device, timeline, nullptr);

	bench_stop_iteration(vulkan.bench);
	test_done(vulkan);
	return 0;
}
