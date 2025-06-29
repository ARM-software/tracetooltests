#include "vulkan_common.h"
#include <inttypes.h>
#include <thread>

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

int main(int argc, char** argv)
{
	vulkan_setup_t vulkan;
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.reqfeat12.timelineSemaphore = VK_TRUE;
	vulkan = test_init(argc, argv, "vulkan_timeline_semaphore_1", reqs);

	VkResult r;

	bench_start_iteration(vulkan.bench);

	VkPhysicalDeviceTimelineSemaphoreProperties pdtsp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES, nullptr };
	VkPhysicalDeviceProperties2 pdp2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &pdtsp };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &pdp2);
	printf("Maximum semaphore value difference on this platform is %lu\n", (unsigned long)pdtsp.maxTimelineSemaphoreValueDifference);

	// Setup resources

	VkSemaphore semaphore;
	VkSemaphore unused_zero_semaphore;
	VkSemaphore unused_one_semaphore;
	VkSemaphoreTypeCreateInfo scsti = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr };
	scsti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	scsti.initialValue = 0;
	VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &scsti };
	sci.flags = 0; // reserved
	r = vkCreateSemaphore(vulkan.device, &sci, nullptr, &semaphore);
	check(r);
	scsti.initialValue = 0;
	r = vkCreateSemaphore(vulkan.device, &sci, nullptr, &unused_zero_semaphore);
	check(r);
	scsti.initialValue = 1;
	r = vkCreateSemaphore(vulkan.device, &sci, nullptr, &unused_one_semaphore);
	check(r);

	uint64_t value = 0xdeadfeef;
	r = vkGetSemaphoreCounterValue(vulkan.device, semaphore, &value);
	check(r);
	assert(value == 0);
	r = vkGetSemaphoreCounterValue(vulkan.device, unused_zero_semaphore, &value);
	check(r);
	assert(value == 0);
	r = vkGetSemaphoreCounterValue(vulkan.device, unused_one_semaphore, &value);
	check(r);
	assert(value == 1);

	VkSemaphoreSignalInfo ssi = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr };
	ssi.semaphore = semaphore;
	ssi.value = 2;
	r = vkSignalSemaphore(vulkan.device, &ssi);

	r = vkGetSemaphoreCounterValue(vulkan.device, semaphore, &value);
	assert(value == 2);

	VkSemaphoreWaitInfo swi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr };
	swi.flags = 0;
	swi.semaphoreCount = 1;
	swi.pSemaphores = &semaphore;
	swi.pValues = &value;
	r = vkWaitSemaphores(vulkan.device, &swi, UINT64_MAX);
	assert(r == VK_SUCCESS);

	value = 4;
	r = vkWaitSemaphores(vulkan.device, &swi, 0);
	assert(r == VK_TIMEOUT);

	std::vector<VkSemaphore> semaphores = { semaphore, unused_zero_semaphore, unused_one_semaphore };
	std::vector<uint64_t> values = { 4, 3, 1 };
	std::vector<VkPipelineStageFlags> flags = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };

	swi.semaphoreCount = 3;
	swi.pSemaphores = semaphores.data();
	swi.pValues = values.data();
	swi.flags = VK_SEMAPHORE_WAIT_ANY_BIT;
	r = vkWaitSemaphores(vulkan.device, &swi, UINT64_MAX);
	assert(r == VK_SUCCESS);

	values[2] = 2;
	r = vkWaitSemaphores(vulkan.device, &swi, 0);
	assert(r == VK_TIMEOUT);

	values[0] = 2;
	values[1] = 0;
	values[2] = 1;
	swi.flags = 0;
	r = vkWaitSemaphores(vulkan.device, &swi, UINT64_MAX);
	assert(r == VK_SUCCESS);

	// enqueue

	VkFence fence;
	VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(r);

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	std::vector<uint64_t> newvalues = { 4, 3, 2 };
	VkTimelineSemaphoreSubmitInfo tssi = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr };
	tssi.waitSemaphoreValueCount = 3;
	tssi.pWaitSemaphoreValues = values.data();
	tssi.signalSemaphoreValueCount = 3;
	tssi.pSignalSemaphoreValues = newvalues.data();
	VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &tssi };
	si.waitSemaphoreCount = 3;
	si.pWaitSemaphores = semaphores.data();
	si.pWaitDstStageMask = flags.data();
	si.commandBufferCount = 0;
	si.pCommandBuffers = nullptr;
	si.signalSemaphoreCount = 3;
	si.pSignalSemaphores = semaphores.data();
	r = vkQueueSubmit(queue, 1, &si, fence); // TODO: also try vkQueueSubmit2 if version != 1.3
	check(r);

	r = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	assert(r == VK_SUCCESS);

	r = vkGetSemaphoreCounterValue(vulkan.device, semaphores.at(0), &value);
	check(r);
	assert(value == 4);
	r = vkGetSemaphoreCounterValue(vulkan.device, semaphores.at(1), &value);
	check(r);
	assert(value == 3);
	r = vkGetSemaphoreCounterValue(vulkan.device, semaphores.at(2), &value);
	check(r);
	assert(value == 2);

	// wrap up

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkDestroySemaphore(vulkan.device, semaphore, nullptr);
	vkDestroySemaphore(vulkan.device, unused_zero_semaphore, nullptr);
	vkDestroySemaphore(vulkan.device, unused_one_semaphore, nullptr);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
