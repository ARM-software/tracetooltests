// Unit test to try out various combinations of GPU memory copying and synchronization with CPU-side
// memory mapping.

#include "vulkan_common.h"

static int queue_variant = 0;
static int map_variant = 0;
static int fence_variant = 0;
static unsigned buffer_size = (32 * 1024);
static unsigned num_buffers = 10;
static vulkan_req_t reqs;
static bool dedicated_allocation = false;

static void show_usage()
{
	printf("-b/--buffer-size N     Set buffer size (default %u)\n", buffer_size);
	printf("-c/--buffer-count N    Set buffer count (default %u)\n", num_buffers);
	printf("-f/--fence-variant N   Set fence variant (default %d)\n", fence_variant);
	printf("\t0 - use vkWaitForFences\n");
	printf("\t1 - use vkGetFenceStatus\n");
	printf("-q/--queue-variant N   Set queue variant (default %d)\n", queue_variant);
	printf("\t0 - many commandbuffers, many queue submit calls\n");
	printf("\t1 - many commandbuffers, one queue submit call with many submits\n");
	printf("\t2 - many commandbuffers, one queue submit\n");
	printf("\t3 - one commandbuffer, one queue submit\n");
	printf("\t4 - many commandbuffers, many queue submit calls\n");
	printf("\t5 - many commandbuffers, many queue submit calls, two queues\n");
	printf("-m/--map-variant N     Set map variant (default %d)\n", map_variant);
	printf("\t0 - memory map kept open\n");
	printf("\t1 - memory map unmapped before submit\n");
	printf("\t2 - memory map remapped to tiny area before submit\n");
	printf("-B/--bufferdeviceaddress Create buffers with known buffer device addresses (requires Vulkan 1.2)\n");
	printf("-D/--dedicatedallocation Create one device memory for each buffer\n");
}

static void waitfence(vulkan_setup_t& vulkan, VkFence fence)
{
	if (fence_variant == 0)
	{
		VkResult result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT32_MAX);
		check(result);
	}
	else if (fence_variant == 1)
	{
		VkResult result = VK_NOT_READY;
		do
		{
			result = vkGetFenceStatus(vulkan.device, fence);
			assert(result != VK_ERROR_DEVICE_LOST);
			if (result != VK_SUCCESS) usleep(10);
		} while (result != VK_SUCCESS);
	}
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-b", "--buffer-size"))
	{
		buffer_size = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-c", "--buffer-count"))
	{
		num_buffers = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-q", "--queue-variant"))
	{
		queue_variant = get_arg(argv, ++i, argc);
		if (queue_variant == 5) reqs.queues = 2;
		return (queue_variant >= 0 && queue_variant <= 5);
	}
	else if (match(argv[i], "-m", "--map-variant"))
	{
		map_variant = get_arg(argv, ++i, argc);
		return (map_variant >= 0 && map_variant <= 2);
	}
	else if (match(argv[i], "-f", "--fence-variant"))
	{
		fence_variant = get_arg(argv, ++i, argc);
		return (fence_variant >= 0 && fence_variant <= 1);
	}
	else if (match(argv[i], "-B", "--bufferdeviceaddress"))
	{
		reqs.bufferDeviceAddress = true;
		return true;
	}
	else if (match(argv[i], "-D", "--dedicatedallocation"))
	{
		dedicated_allocation = true;
		return true;
	}
	return false;
}

static void copying_1(int argc, char** argv)
{
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_copying_1", reqs);
	VkResult result;

	VkQueue queue1;
	VkQueue queue2;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue1);
	vkGetDeviceQueue(vulkan.device, 0, (queue_variant == 5) ? 1 : 0, &queue2);

	std::vector<VkBuffer> origin_buffers(num_buffers);
	std::vector<VkBuffer> target_buffers(num_buffers);
	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (reqs.bufferDeviceAddress)
	{
		bufferCreateInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
	}
	VkBufferCreateInfo bufferCreateInfo2 = bufferCreateInfo;
	bufferCreateInfo2.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	for (unsigned i = 0; i < num_buffers; i++)
	{
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &origin_buffers.at(i));
		assert(result == VK_SUCCESS);
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo2, nullptr, &target_buffers.at(i));
		assert(result == VK_SUCCESS);
	}

	std::vector<VkDeviceMemory> origin_memory;
	std::vector<VkDeviceMemory> target_memory;
	uint32_t origin_aligned_size = testAllocateBufferMemory(vulkan, origin_buffers, origin_memory, reqs.bufferDeviceAddress, dedicated_allocation, true, "origin");
	uint32_t target_aligned_size = testAllocateBufferMemory(vulkan, target_buffers, target_memory, reqs.bufferDeviceAddress, dedicated_allocation, false, "target");
	assert(origin_aligned_size == target_aligned_size);

	if (dedicated_allocation && (map_variant == 0 || map_variant == 2))
	{
		char* data = nullptr;
		for (unsigned i = 0; i < origin_buffers.size(); i++)
		{
			result = vkMapMemory(vulkan.device, origin_memory[i], (map_variant == 2) ? 10 : 0, (map_variant == 2) ? 20 : origin_aligned_size, 0, (void**)&data);
			assert(result == VK_SUCCESS);
		}
	}
	else if (map_variant == 0 || map_variant == 2)
	{
		char* data = nullptr;
		result = vkMapMemory(vulkan.device, origin_memory[0], (map_variant == 2) ? 10 : 0, (map_variant == 2) ? 20 : num_buffers * origin_aligned_size, 0, (void**)&data);
		assert(result == VK_SUCCESS);
	}

	for (unsigned i = 0; i < num_buffers && reqs.bufferDeviceAddress; i++)
	{
		VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, origin_buffers[i] };
		VkDeviceAddress a = vkGetBufferDeviceAddress(vulkan.device, &info);
		assert(a != 0);
	}

	VkCommandPoolCreateInfo command_pool_create_info = {};
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = 0; // TBD - add transfer queue variant

	VkCommandPool command_pool;
	result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, NULL, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.commandBufferCount = num_buffers + 1;
	std::vector<VkCommandBuffer> command_buffers(num_buffers + 1);
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, command_buffers.data());
	check(result);
	VkCommandBufferBeginInfo command_buffer_begin_info = {};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VkFence fence;
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	result = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(result);
	// Many commands
	for (unsigned i = 0; i < num_buffers; i++)
	{
		result = vkBeginCommandBuffer(command_buffers[i], &command_buffer_begin_info);
		check(result);
		testCmdCopyBuffer(vulkan, command_buffers.at(i), std::vector<VkBuffer>{ origin_buffers.at(i) }, std::vector<VkBuffer>{ target_buffers.at(i) }, buffer_size);
		result = vkEndCommandBuffer(command_buffers[i]);
		check(result);
	}
	// Single command
	result = vkBeginCommandBuffer(command_buffers.at(num_buffers), &command_buffer_begin_info);
	check(result);
	testCmdCopyBuffer(vulkan, command_buffers.at(num_buffers), origin_buffers, target_buffers, buffer_size);
	result = vkEndCommandBuffer(command_buffers.at(num_buffers));
	check(result);
	if (queue_variant == 0 || queue_variant == 4 || queue_variant == 5)
	{
		std::vector<VkFence> fences(num_buffers);
		for (unsigned i = 0; i < num_buffers; i++)
		{
			result = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fences[i]);
			check(result);
		}
		for (unsigned i = 0; i < num_buffers; i++)
		{
			VkSubmitInfo submit_info = {};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffers[i];
			result = vkQueueSubmit(i % 2 == 0 ? queue1 : queue2, 1, &submit_info, fences[i]);
			check(result);
			if (queue_variant == 0) waitfence(vulkan, fences[i]);
		}
		if (queue_variant == 4 || queue_variant == 5)
		{
			result = vkWaitForFences(vulkan.device, num_buffers, fences.data(), VK_TRUE, UINT64_MAX);
			check(result);
		}
		result = vkResetFences(vulkan.device, num_buffers, fences.data());
		check(result);
		for (unsigned i = 0; i < num_buffers; i++)
		{
			vkDestroyFence(vulkan.device, fences[i], nullptr);
		}
	}
	else if (queue_variant == 1) // slighly "better" version
	{
		std::vector<VkSubmitInfo> submit_info(num_buffers); // doing only one submission call!
		for (unsigned i = 0; i < num_buffers; i++)
		{
			submit_info[i].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info[i].commandBufferCount = 1;
			submit_info[i].pCommandBuffers = &command_buffers[i];
		}
		result = vkQueueSubmit(queue1, num_buffers, submit_info.data(), fence);
		check(result);
		waitfence(vulkan, fence); // only one fence...
	}
	else if (queue_variant == 2) // interesting variant, serialize all the copies, but still one cmdbuffer per copy
	{
		VkSubmitInfo submit_info = {}; // doing only one submission call...
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = num_buffers;
		submit_info.pCommandBuffers = command_buffers.data(); // ... and only one submission of N command buffers
		result = vkQueueSubmit(queue1, 1, &submit_info, fence);
		check(result);
		waitfence(vulkan, fence); // only one fence...
	}
	else if (queue_variant == 3) // probably the version that makes the most sense, all copy commands in one cmdbuffer, one submit
	{
		VkSubmitInfo submit_info = {}; // doing only one submission call...
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffers[num_buffers]; // ... and only one submission
		result = vkQueueSubmit(queue1, 1, &submit_info, fence);
		check(result);
		waitfence(vulkan, fence); // only one fence...
	}

	// Verification
	if (vulkan.vkAssertBuffer)
	{
		for (unsigned i = 0; i < num_buffers; i++)
		{
			const uint32_t orig = vulkan.vkAssertBuffer(vulkan.device, origin_buffers.at(i));
			const uint32_t dest = vulkan.vkAssertBuffer(vulkan.device, target_buffers.at(i));
			assert(orig == dest);
		}
	}

	// Cleanup...
	if (map_variant == 0 || map_variant == 2) for (unsigned i = 0; i < origin_memory.size(); i++) vkUnmapMemory(vulkan.device, origin_memory[i]);
	vkDestroyFence(vulkan.device, fence, nullptr);
	for (unsigned i = 0; i < num_buffers; i++)
	{
		vkDestroyBuffer(vulkan.device, origin_buffers.at(i), nullptr);
		vkDestroyBuffer(vulkan.device, target_buffers.at(i), nullptr);
	}

	for (unsigned i = 0; i < origin_memory.size(); i++) testFreeMemory(vulkan, origin_memory[i]);
	for (unsigned i = 0; i < target_memory.size(); i++) testFreeMemory(vulkan, target_memory[i]);
	vkFreeCommandBuffers(vulkan.device, command_pool, num_buffers + 1, command_buffers.data());
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	test_done(vulkan);
}

int main(int argc, char** argv)
{
	copying_1(argc, argv);
	return 0;
}
