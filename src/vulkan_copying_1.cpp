// Unit test to try out various combinations of GPU memory copying and synchronization with CPU-side
// memory mapping.

#include "vulkan_common.h"

static int queue_variant = 0;
static int map_variant = 0;
static int fence_variant = 0;
static unsigned buffer_size = (32 * 1024);
static unsigned num_buffers = 10;

void usage()
{
	printf("Usage: vulkan_copying_1\n");
	printf("-h/--help              This help\n");
	printf("-g/--gpu level N       Select GPU (default 0)\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default %d)\n", p__debug_level);
	printf("-b/--buffer-size N     Set buffer size (default %d)\n", buffer_size);
	printf("-c/--buffer-count N    Set buffer count (default %d)\n", num_buffers);
	printf("-f/--fence-variant N   Set fence variant (default %d)\n", fence_variant);
	printf("\t0 - use vkWaitForFences\n");
	printf("\t1 - use vkGetFenceStatus\n");
	printf("-q/--queue-variant N   Set queue variant (default %d)\n", queue_variant);
	printf("\t0 - many commandbuffers, many queue submit calls, many flushes\n");
	printf("\t1 - many commandbuffers, one queue submit call with many submits, one flush\n");
	printf("\t2 - many commandbuffers, one queue submit, one flush\n");
	printf("\t3 - one commandbuffer, one queue submit, no flush\n");
	printf("\t4 - many commandbuffers, many queue submit calls, no flush\n");
	printf("\t5 - many commandbuffers, many queue submit calls, no flush, two queues\n");
	printf("-m/--map-variant N     Set map variant (default %d)\n", map_variant);
	printf("\t0 - memory map kept open\n");
	printf("\t1 - memory map unmapped before submit\n");
	printf("\t2 - memory map remapped to tiny area before submit\n");
	exit(-1);
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

static void copying_1()
{
	vulkan_req_t reqs;
	reqs.queues = (queue_variant == 5) ? 2 : 1;
	vulkan_setup_t vulkan = test_init("vulkan_copying_1", reqs);
	VkResult result;

	VkQueue queue1;
	VkQueue queue2;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue1);
	vkGetDeviceQueue(vulkan.device, 0, queue_variant == 5 ? 1 : 0, &queue2);

	std::vector<VkBuffer> origin_buffers(num_buffers);
	std::vector<VkBuffer> target_buffers(num_buffers);
	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkBufferCreateInfo bufferCreateInfo2 = bufferCreateInfo;
	bufferCreateInfo2.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	for (unsigned i = 0; i < num_buffers; i++)
	{
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &origin_buffers.at(i));
		assert(result == VK_SUCCESS);
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo2, nullptr, &target_buffers.at(i));
		assert(result == VK_SUCCESS);
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan.device, origin_buffers.at(0), &memory_requirements);
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
	const uint32_t aligned_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo pAllocateMemInfo = {};
	pAllocateMemInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_size * num_buffers;
	VkDeviceMemory origin_memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &origin_memory);
	assert(result == VK_SUCCESS);
	assert(origin_memory != VK_NULL_HANDLE);
	VkDeviceMemory target_memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &target_memory);
	assert(result == VK_SUCCESS);
	assert(target_memory != VK_NULL_HANDLE);

	VkDeviceSize offset = 0;
	for (unsigned i = 0; i < num_buffers; i++)
	{
		vkBindBufferMemory(vulkan.device, origin_buffers.at(i), origin_memory, offset);
		vkBindBufferMemory(vulkan.device, target_buffers.at(i), target_memory, offset);
		offset += aligned_size;
	}

	char* data = nullptr;
	result = vkMapMemory(vulkan.device, origin_memory, 0, num_buffers * aligned_size, 0, (void**)&data);
	assert(result == VK_SUCCESS);
	offset = 0;
	for (unsigned i = 0; i < num_buffers; i++)
	{
		memset(data + offset, i, aligned_size);
		offset += aligned_size;
	}
	if (map_variant == 1 || map_variant == 2) vkUnmapMemory(vulkan.device, origin_memory);
	if (map_variant == 2) vkMapMemory(vulkan.device, origin_memory, 10, 20, 0, (void**)&data);

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
	VkMemoryBarrier memory_barrier = {};
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	// Many commands
	for (unsigned i = 0; i < num_buffers; i++)
	{
		result = vkBeginCommandBuffer(command_buffers[i], &command_buffer_begin_info);
		check(result);
		VkBufferCopy region;
		region.srcOffset = 0;
		region.dstOffset = 0;
		region.size = buffer_size;
		vkCmdCopyBuffer(command_buffers[i], origin_buffers[i], target_buffers[i], 1, &region);
		vkCmdPipelineBarrier(command_buffers[i], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
		result = vkEndCommandBuffer(command_buffers[i]);
		check(result);
	}
	// Single command
	result = vkBeginCommandBuffer(command_buffers.at(num_buffers), &command_buffer_begin_info);
	check(result);
	for (unsigned i = 0; i < num_buffers; i++)
	{
		VkBufferCopy region;
		region.srcOffset = 0;
		region.dstOffset = 0;
		region.size = buffer_size;
		vkCmdCopyBuffer(command_buffers.at(num_buffers), origin_buffers[i], target_buffers[i], 1, &region);
	}
	vkCmdPipelineBarrier(command_buffers.at(num_buffers), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
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
			if (queue_variant == 0 && map_variant == 0)
			{
				VkMappedMemoryRange range = {};
				range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
				range.memory = origin_memory;
				range.size = aligned_size;
				range.offset = aligned_size * i;
				result = vkFlushMappedMemoryRanges(vulkan.device, 1, &range);
				check(result);
			}
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
		if (map_variant != 1)
		{
			VkMappedMemoryRange range = {}; // and only one flush call!
			range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			range.memory = origin_memory;
			range.size = VK_WHOLE_SIZE;
			result = vkFlushMappedMemoryRanges(vulkan.device, 1, &range);
			check(result);
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
		if (map_variant != 1)
		{
			VkMappedMemoryRange range = {}; // and only one flush call! not N calls to flush the entire memory area...
			range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			range.memory = origin_memory;
			range.size = VK_WHOLE_SIZE;
			result = vkFlushMappedMemoryRanges(vulkan.device, 1, &range);
			check(result);
		}
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
	if (map_variant == 0 || map_variant == 2) vkUnmapMemory(vulkan.device, origin_memory);
	vkDestroyFence(vulkan.device, fence, nullptr);
	for (unsigned i = 0; i < num_buffers; i++)
	{
		vkDestroyBuffer(vulkan.device, origin_buffers.at(i), nullptr);
		vkDestroyBuffer(vulkan.device, target_buffers.at(i), nullptr);
	}

	testFreeMemory(vulkan, origin_memory);
	testFreeMemory(vulkan, target_memory);
	vkFreeCommandBuffers(vulkan.device, command_pool, num_buffers + 1, command_buffers.data());
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	test_done(vulkan);
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++)
	{
		if (match(argv[i], "-h", "--help"))
		{
			usage();
		}
		else if (match(argv[i], "-d", "--debug"))
		{
			p__debug_level = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-g", "--gpu"))
		{
			select_gpu(get_arg(argv, ++i, argc));
		}
		else if (match(argv[i], "-b", "--buffer-size"))
		{
			buffer_size = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-c", "--buffer-count"))
		{
			num_buffers = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-q", "--queue-variant"))
		{
			queue_variant = get_arg(argv, ++i, argc);
			if (queue_variant < 0 || queue_variant > 5)
			{
				usage();
			}
		}
		else if (match(argv[i], "-m", "--map-variant"))
		{
			map_variant = get_arg(argv, ++i, argc);
			if (map_variant < 0 || map_variant > 2)
			{
				usage();
			}
		}
		else if (match(argv[i], "-f", "--fence-variant"))
		{
			fence_variant = get_arg(argv, ++i, argc);
			if (fence_variant < 0 || fence_variant > 1)
			{
				usage();
			}
		}
		else
		{
			ELOG("Unrecognized cmd line parameter: %s", argv[i]);
			return -1;
		}
	}
	printf("Running with queue variant %d, map variant %d, fence variant %d\n", queue_variant, map_variant, fence_variant);
	copying_1();
	return 0;
}
