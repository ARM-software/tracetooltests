#include "vulkan_common.h"

#include <atomic>
#include <thread>

struct SignalThreadArgs
{
	VkDevice device = VK_NULL_HANDLE;
	VkSemaphore semaphore = VK_NULL_HANDLE;
	uint64_t value = 0;
	std::atomic_bool started { false };
};

static void signal_timeline_thread(SignalThreadArgs* args)
{
	args->started.store(true);
	VkSemaphoreSignalInfo signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr };
	signal_info.semaphore = args->semaphore;
	signal_info.value = args->value;
	VkResult result = vkSignalSemaphore(args->device, &signal_info);
	check(result);
}

static VkSemaphore create_binary_semaphore(VkDevice device)
{
	VkSemaphoreCreateInfo create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	VkSemaphore semaphore = VK_NULL_HANDLE;
	VkResult result = vkCreateSemaphore(device, &create_info, nullptr, &semaphore);
	check(result);
	return semaphore;
}

static VkSemaphore create_timeline_semaphore(VkDevice device, uint64_t initial_value)
{
	VkSemaphoreTypeCreateInfo type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr };
	type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	type_info.initialValue = initial_value;
	VkSemaphoreCreateInfo create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type_info };
	VkSemaphore semaphore = VK_NULL_HANDLE;
	VkResult result = vkCreateSemaphore(device, &create_info, nullptr, &semaphore);
	check(result);
	return semaphore;
}

static VkFence create_fence(VkDevice device)
{
	VkFenceCreateInfo create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fence = VK_NULL_HANDLE;
	VkResult result = vkCreateFence(device, &create_info, nullptr, &fence);
	check(result);
	return fence;
}

static void signal_timeline(VkDevice device, VkSemaphore semaphore, uint64_t value)
{
	VkSemaphoreSignalInfo signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr };
	signal_info.semaphore = semaphore;
	signal_info.value = value;
	VkResult result = vkSignalSemaphore(device, &signal_info);
	check(result);
}

static void wait_fence(VkDevice device, VkFence fence)
{
	VkResult result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.reqfeat12.timelineSemaphore = VK_TRUE;
	reqs.queues = 2;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_strict_sync", reqs);
	bench_start_iteration(vulkan.bench);

	VkQueue blocked_queue = VK_NULL_HANDLE;
	VkQueue signal_queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &blocked_queue);
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 1, &signal_queue);
	assert(blocked_queue != VK_NULL_HANDLE);
	assert(signal_queue != VK_NULL_HANDLE);

	VkSemaphore gate = create_timeline_semaphore(vulkan.device, 0);
	VkSemaphore binary = create_binary_semaphore(vulkan.device);
	VkFence gate_fence = create_fence(vulkan.device);
	VkFence fifo_fence = create_fence(vulkan.device);
	VkFence binary_fence = create_fence(vulkan.device);

	uint64_t gate_value = 1;
	uint64_t binary_signal_value = 0;
	VkTimelineSemaphoreSubmitInfo timeline_wait = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr };
	timeline_wait.waitSemaphoreValueCount = 1;
	timeline_wait.pWaitSemaphoreValues = &gate_value;
	timeline_wait.signalSemaphoreValueCount = 1;
	timeline_wait.pSignalSemaphoreValues = &binary_signal_value;
	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkSubmitInfo blocked_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_wait };
	blocked_submit.waitSemaphoreCount = 1;
	blocked_submit.pWaitSemaphores = &gate;
	blocked_submit.pWaitDstStageMask = &wait_stage;
	blocked_submit.signalSemaphoreCount = 1;
	blocked_submit.pSignalSemaphores = &binary;
	VkResult result = vkQueueSubmit(blocked_queue, 1, &blocked_submit, gate_fence);
	check(result);

	VkSubmitInfo empty_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	result = vkQueueSubmit(blocked_queue, 1, &empty_submit, fifo_fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fifo_fence, VK_TRUE, 0);
	assert(result == VK_TIMEOUT);

	VkSubmitInfo binary_wait = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	binary_wait.waitSemaphoreCount = 1;
	binary_wait.pWaitSemaphores = &binary;
	binary_wait.pWaitDstStageMask = &wait_stage;
	signal_timeline(vulkan.device, gate, 1);
	result = vkQueueSubmit(signal_queue, 1, &binary_wait, binary_fence);
	check(result);
	wait_fence(vulkan.device, gate_fence);
	wait_fence(vulkan.device, fifo_fence);
	wait_fence(vulkan.device, binary_fence);

	VkSemaphore submit2_timeline = create_timeline_semaphore(vulkan.device, 0);
	VkFence submit2_fence = create_fence(vulkan.device);
	VkSemaphoreSubmitInfo signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr };
	signal_info.semaphore = submit2_timeline;
	signal_info.value = 2;
	signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	VkSubmitInfo2 submit2 = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr };
	submit2.signalSemaphoreInfoCount = 1;
	submit2.pSignalSemaphoreInfos = &signal_info;
	result = vkQueueSubmit2(blocked_queue, 1, &submit2, submit2_fence);
	check(result);
	wait_fence(vulkan.device, submit2_fence);
	uint64_t counter = 0;
	result = vkGetSemaphoreCounterValue(vulkan.device, submit2_timeline, &counter);
	check(result);
	assert(counter == 2);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &pool);
	check(result);
	VkCommandBufferAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	allocate_info.commandPool = pool;
	allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &allocate_info, &command_buffer);
	check(result);
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);
	result = vkEndCommandBuffer(command_buffer);
	check(result);

	gate_value = 2;
	timeline_wait.pWaitSemaphoreValues = &gate_value;
	blocked_submit.signalSemaphoreCount = 0;
	blocked_submit.pSignalSemaphores = nullptr;
	timeline_wait.signalSemaphoreValueCount = 0;
	timeline_wait.pSignalSemaphoreValues = nullptr;
	blocked_submit.commandBufferCount = 1;
	blocked_submit.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(blocked_queue, 1, &blocked_submit, VK_NULL_HANDLE);
	check(result);
	empty_submit.commandBufferCount = 1;
	empty_submit.pCommandBuffers = &command_buffer;
	VkFence simultaneous_fence = create_fence(vulkan.device);
	result = vkQueueSubmit(blocked_queue, 1, &empty_submit, simultaneous_fence);
	check(result);
	signal_timeline(vulkan.device, gate, 2);
	wait_fence(vulkan.device, simultaneous_fence);
	result = vkResetCommandBuffer(command_buffer, 0);
	check(result);

	VkSemaphore thread_timeline = create_timeline_semaphore(vulkan.device, 0);
	SignalThreadArgs thread_args;
	thread_args.device = vulkan.device;
	thread_args.semaphore = thread_timeline;
	thread_args.value = 1;
	std::thread signal_thread(signal_timeline_thread, &thread_args);
	while (!thread_args.started.load()) std::this_thread::yield();
	VkSemaphoreWaitInfo host_wait = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr };
	host_wait.semaphoreCount = 1;
	host_wait.pSemaphores = &thread_timeline;
	host_wait.pValues = &thread_args.value;
	result = vkWaitSemaphores(vulkan.device, &host_wait, UINT64_MAX);
	check(result);
	signal_thread.join();

	vkDestroySemaphore(vulkan.device, thread_timeline, nullptr);
	vkDestroyFence(vulkan.device, simultaneous_fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, pool, nullptr);
	vkDestroyFence(vulkan.device, submit2_fence, nullptr);
	vkDestroySemaphore(vulkan.device, submit2_timeline, nullptr);
	vkDestroyFence(vulkan.device, binary_fence, nullptr);
	vkDestroyFence(vulkan.device, fifo_fence, nullptr);
	vkDestroyFence(vulkan.device, gate_fence, nullptr);
	vkDestroySemaphore(vulkan.device, binary, nullptr);
	vkDestroySemaphore(vulkan.device, gate, nullptr);

	bench_stop_iteration(vulkan.bench);
	test_done(vulkan);
	return 0;
}
