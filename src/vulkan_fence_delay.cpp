#include "vulkan_common.h"
#include <inttypes.h>
#include <thread>

enum class FenceDelayUnit
{
	Calls,
	Frames
};

static FenceDelayUnit fence_delay_unit = FenceDelayUnit::Calls;
static uint64_t fence_delay_threshold = 0;
static vulkan_req_t reqs;
static vulkan_setup_t vulkan;
static const std::chrono::milliseconds sleep_duration(1);

static void show_usage()
{
	printf("-f/--fence-delay <N>            If set, assume that the capture tool is introducing a fence delay of N calls. (Default 0).\n");
	printf("-u/--fence-delay-unit <unit>    Specify what unit is used for the fence delay. Accepted values are (calls, frames). (Default calls).\n");
	printf("-t/--fence-delay-threshold <N>  Specify the timeout threshold in nanoseconds under which a vkWaitForFences call is delayed. (Default 0).\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-f", "--fence-delay"))
	{
		reqs.fence_delay = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-u", "--fence-delay-unit"))
	{
		const char* unit = get_string_arg(argv, ++i, argc);
		if (!strcmp(unit, "calls"))
		{
			fence_delay_unit = FenceDelayUnit::Calls;
			return true;
		}
		else if (!strcmp(unit, "frames"))
		{
			fence_delay_unit = FenceDelayUnit::Frames;
			return true;
		}
	}
	else if (match(argv[i], "-t", "--fence-delay-threshold"))
	{
		fence_delay_threshold = get_arg(argv, ++i, argc);
		return true;
	}
	return false;
}

static void resubmitFence(VkFence fence)
{
	static VkQueue queue = VK_NULL_HANDLE;
	if (queue == VK_NULL_HANDLE)
	{
		vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	}

	VkResult r = vkResetFences(vulkan.device, 1, &fence);
	check(r);

	r = vkQueueSubmit(queue, 0, nullptr, fence);
	check(r);

	std::this_thread::sleep_for(sleep_duration);
}

static void submitFrame()
{
	static VkFrameBoundaryEXT frameBoundary = {};
	static VkSubmitInfo submitInfo = {};
	static VkQueue queue = VK_NULL_HANDLE;

	if (submitInfo.sType == 0)
	{
		frameBoundary.sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT;
		frameBoundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;

		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pNext = &frameBoundary;

		vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	}

	++frameBoundary.frameID;
	VkResult r = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
	check(r);

	std::this_thread::sleep_for(sleep_duration);
}

int main(int argc, char** argv)
{
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.device_extensions.push_back("VK_EXT_frame_boundary");
	vulkan = test_init(argc, argv, "vulkan_fence_delay", reqs);

	VkResult r;

	bench_start_iteration(vulkan.bench);

	// Setup resources

	VkFence fences[2];
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, fences);
	check(r);
	r = vkCreateFence(vulkan.device, &fence_create_info, NULL, fences + 1);
	check(r);

	VkFence& fence = fences[0];
	VkFence& fence1 = fences[0];
	VkFence& fence2 = fences[1];

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	// Test vkGetFenceStatus delay for 1 fence

	resubmitFence(fence);

	for (uint64_t i = 0; i < reqs.fence_delay; ++i)
	{
		r = vkGetFenceStatus(vulkan.device, fence);
		assert(r == VK_NOT_READY);

		submitFrame();
	}

	r = vkGetFenceStatus(vulkan.device, fence);
	assert(r == VK_SUCCESS);

	// Test "unit"

	resubmitFence(fence);

	if (fence_delay_unit == FenceDelayUnit::Calls)
	{
		for (uint64_t i = 0; i < reqs.fence_delay + 1; ++i)
		{
			submitFrame();
		}

		for (uint64_t i = 0; i < reqs.fence_delay; ++i)
		{
			r = vkGetFenceStatus(vulkan.device, fence);
			assert(r == VK_NOT_READY);

			submitFrame();
		}

		r = vkGetFenceStatus(vulkan.device, fence);
		assert(r == VK_SUCCESS);
	}
	else if (fence_delay_unit == FenceDelayUnit::Frames)
	{
		for (uint64_t i = 0; i < reqs.fence_delay + 1; ++i)
		{
			r = vkGetFenceStatus(vulkan.device, fence);
			assert(r == VK_NOT_READY);
		}

		for (uint64_t i = 0; i < reqs.fence_delay; ++i)
		{
			r = vkGetFenceStatus(vulkan.device, fence);
			assert(r == VK_NOT_READY);

			submitFrame();
		}

		r = vkGetFenceStatus(vulkan.device, fence);
		assert(r == VK_SUCCESS);
	}

	// Test vkWaitForFences delay under threshold for 1 fence

	resubmitFence(fence);

	for (uint64_t i = 0; i < reqs.fence_delay; ++i)
	{
		r = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, fence_delay_threshold);
		assert(r == VK_TIMEOUT);

		submitFrame();
	}

	r = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, fence_delay_threshold);
	assert(r == VK_SUCCESS);

	// Test vkWaitForFences delay over threshold and subsequent fence queries

	resubmitFence(fence);
	r = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, fence_delay_threshold + 1);
	assert(r == VK_SUCCESS);
	r = vkGetFenceStatus(vulkan.device, fence);
	assert(r == VK_SUCCESS);
	r = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, fence_delay_threshold);
	assert(r == VK_SUCCESS);

	// Test mix of vkGetFenceStatus and vkWaitForFences for multiple fences

	resubmitFence(fence1);
	resubmitFence(fence2);

	for (uint64_t i = 0; i < reqs.fence_delay; ++i)
	{
		if (i & 1)
		{
			r = vkWaitForFences(vulkan.device, 1, &fence1, VK_TRUE, fence_delay_threshold);
			assert(r == VK_TIMEOUT);
			r = vkGetFenceStatus(vulkan.device, fence2);
			assert(r == VK_NOT_READY);
		}
		else
		{
			r = vkGetFenceStatus(vulkan.device, fence1);
			assert(r == VK_NOT_READY);
			r = vkWaitForFences(vulkan.device, 1, &fence2, VK_TRUE, fence_delay_threshold);
			assert(r == VK_TIMEOUT);
		}
		submitFrame();
	}

	r = vkGetFenceStatus(vulkan.device, fence1);
	assert(r == VK_SUCCESS);
	r = vkGetFenceStatus(vulkan.device, fence2);
	assert(r == VK_SUCCESS);

	// Test one fence being queried first

	resubmitFence(fence1);
	resubmitFence(fence2);

	for (uint64_t i = 0; i < reqs.fence_delay; ++i)
	{
		r = vkGetFenceStatus(vulkan.device, fence1);
		assert(r == VK_NOT_READY);
		submitFrame();
	}

	r = vkGetFenceStatus(vulkan.device, fence1);
	assert(r == VK_SUCCESS);

	for (uint64_t i = 0; i < reqs.fence_delay; ++i)
	{
		r = vkGetFenceStatus(vulkan.device, fence1);
		assert(r == VK_SUCCESS);
		r = vkGetFenceStatus(vulkan.device, fence2);
		if (fence_delay_unit == FenceDelayUnit::Calls)
		{
			assert(r == VK_NOT_READY);
		}
		else if (fence_delay_unit == FenceDelayUnit::Frames)
		{
			assert(r == VK_SUCCESS);    // Because then we already submitted the N frames in the first loop, and fence are forced to be decremented at the same time
		}
	}
	
	r = vkGetFenceStatus(vulkan.device, fence1);
	assert(r == VK_SUCCESS);
	r = vkGetFenceStatus(vulkan.device, fence2);
	assert(r == VK_SUCCESS);

	// Test one fence waited for above threshold and one fence being normally queried

	resubmitFence(fence1);
	resubmitFence(fence2);

	r = vkWaitForFences(vulkan.device, 1, &fence1, VK_TRUE, fence_delay_threshold + 1);
	assert(r == VK_SUCCESS);

	for (uint64_t i = 0; i < reqs.fence_delay; ++i)
	{
		r = vkGetFenceStatus(vulkan.device, fence1);
		assert(r == VK_SUCCESS);
		r = vkGetFenceStatus(vulkan.device, fence2);
		assert(r == VK_NOT_READY);
		submitFrame();
	}
	
	r = vkGetFenceStatus(vulkan.device, fence1);
	assert(r == VK_SUCCESS);
	r = vkGetFenceStatus(vulkan.device, fence2);
	assert(r == VK_SUCCESS);

	// Test two fences being waited for under threshold in the same call

	resubmitFence(fence1);
	resubmitFence(fence2);

	for (uint64_t i = 0; i < reqs.fence_delay; ++i)
	{
		if (i & 1)
		{
			r = vkWaitForFences(vulkan.device, 2, fences, VK_TRUE, fence_delay_threshold);
			assert(r == VK_TIMEOUT);
		}
		else
		{
			r = vkGetFenceStatus(vulkan.device, fence1);
			assert(r == VK_NOT_READY);
			r = vkGetFenceStatus(vulkan.device, fence2);
			assert(r == VK_NOT_READY);
		}
		submitFrame();
	}
	
	r = vkGetFenceStatus(vulkan.device, fence1);
	assert(r == VK_SUCCESS);
	r = vkGetFenceStatus(vulkan.device, fence2);
	assert(r == VK_SUCCESS);

	// Test two fences being waited for above threshold in the same call

	resubmitFence(fence1);
	resubmitFence(fence2);

	r = vkWaitForFences(vulkan.device, 2, fences, VK_TRUE, fence_delay_threshold + 1);
	assert(r == VK_SUCCESS);
	r = vkGetFenceStatus(vulkan.device, fence1);
	assert(r == VK_SUCCESS);
	r = vkGetFenceStatus(vulkan.device, fence2);
	assert(r == VK_SUCCESS);

	// Test fences not being submitted at the same time

	resubmitFence(fence1);

	for (uint64_t i = 0; i < reqs.fence_delay / 2; ++i)
	{
		r = vkGetFenceStatus(vulkan.device, fence1);
		assert(r == VK_NOT_READY);
		r = vkGetFenceStatus(vulkan.device, fence2);
		assert(r == VK_SUCCESS);
		submitFrame();
	}

	resubmitFence(fence2);

	for (uint64_t i = 0; i < reqs.fence_delay - reqs.fence_delay / 2; ++i)
	{
		r = vkGetFenceStatus(vulkan.device, fence1);
		assert(r == VK_NOT_READY);
		r = vkGetFenceStatus(vulkan.device, fence2);
		assert(r == VK_NOT_READY);
		submitFrame();
	}

	for (uint64_t i = 0; i < reqs.fence_delay / 2; ++i)
	{
		r = vkGetFenceStatus(vulkan.device, fence1);
		assert(r == VK_SUCCESS);
		r = vkGetFenceStatus(vulkan.device, fence2);
		assert(r == VK_NOT_READY);
		submitFrame();
	}
	
	r = vkGetFenceStatus(vulkan.device, fence1);
	assert(r == VK_SUCCESS);
	r = vkGetFenceStatus(vulkan.device, fence2);
	assert(r == VK_SUCCESS);
	
	// Ends the test

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
