// Unit test to try out various combinations of ping-ponging jobs between two queues with dependencies.

#include "vulkan_common.h"

static int fence_variant = 0;
static int map_variant = 0;
static int queue_variant = 0;
static unsigned buffer_size = (32 * 1024);
static unsigned num_buffers = 10;
static int times = repeats();

void usage()
{
	printf("Usage: vulkan_copying_1\n");
	printf("-h/--help              This help\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default %d)\n", p__debug_level);
	printf("-b/--buffer-size N     Set buffer size (default %d)\n", buffer_size);
	printf("-c/--buffer-count N    Set buffer count (default %d)\n", num_buffers);
	printf("-t/--times N           Times to repeat (default %d)\n", times);
	printf("-q/--queue-variant N   Set queue variant (default %d)\n", queue_variant);
	printf("\t0 - ping-pong between two queues\n");
	printf("\t1 - put all jobs on one queue\n");
	printf("-f/--fence-variant N   Set fence variant (default %d)\n", fence_variant);
	printf("\t0 - wait for fences each loop\n");
	printf("\t1 - do not wait for fences\n");
	printf("-m/--map-variant N     Set map variant (default %d)\n", map_variant);
	printf("\t0 - memory map kept open\n");
	printf("\t1 - memory map unmapped before submit\n");
	printf("\t2 - memory map remapped to tiny area before submit\n");
	exit(-1);
}

static void copying_2()
{
	vulkan_setup_t vulkan = test_init("copying_2");
	VkResult result;

	VkQueue queue1;
	VkQueue queue2;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue1);
	vkGetDeviceQueue(vulkan.device, 0, 1, &queue2);

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
	command_buffer_allocate_info.commandBufferCount = num_buffers;
	std::vector<VkCommandBuffer> command_buffers(num_buffers);
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, command_buffers.data());
	check(result);
	VkCommandBufferBeginInfo command_buffer_begin_info = {};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VkMemoryBarrier memory_barrier = {};
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	std::vector<VkSemaphore> semaphores(num_buffers);
	std::vector<VkFence> fences(num_buffers);
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

		VkSemaphoreCreateInfo seminfo = {};
		seminfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		seminfo.pNext = nullptr;
		seminfo.flags = 0;
		result = vkCreateSemaphore(vulkan.device, &seminfo, nullptr, &semaphores.at(i));

		VkFenceCreateInfo fence_create_info = {};
		fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		result = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fences[i]);
		check(result);
	}

	for (int i = 0; i < times; i++)
	{
		for (unsigned i = 0; i < num_buffers; i++)
		{
			if (map_variant == 0)
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
			submit_info.signalSemaphoreCount = 1;
			submit_info.pSignalSemaphores = &semaphores[i];
			if (i > 0)
			{
				submit_info.waitSemaphoreCount = 1;
				submit_info.pWaitSemaphores = &semaphores.at(i - 1);
				VkPipelineStageFlags flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
				submit_info.pWaitDstStageMask = &flags;
			}
			VkQueue q = queue1;
			if (queue_variant == 0 && i % 2 == 1) q = queue2; // interleave mode
			result = vkQueueSubmit(q, 1, &submit_info, fences[i]);
			check(result);
		}
		if (fence_variant == 0)
		{
			result = vkWaitForFences(vulkan.device, num_buffers, fences.data(), VK_TRUE, UINT64_MAX);
			check(result);
			result = vkResetFences(vulkan.device, num_buffers, fences.data());
			check(result);
		}
	}

	// Cleanup...
	if (map_variant == 0 || map_variant == 2) vkUnmapMemory(vulkan.device, origin_memory);
	for (unsigned i = 0; i < num_buffers; i++)
	{
		vkDestroyBuffer(vulkan.device, origin_buffers.at(i), nullptr);
		vkDestroyBuffer(vulkan.device, target_buffers.at(i), nullptr);
		vkDestroySemaphore(vulkan.device, semaphores.at(i), nullptr);
		vkDestroyFence(vulkan.device, fences[i], nullptr);
	}
	vkFreeMemory(vulkan.device, origin_memory, nullptr);
	vkFreeMemory(vulkan.device, target_memory, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, num_buffers, command_buffers.data());
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
		else if (match(argv[i], "-b", "--buffer-size"))
		{
			buffer_size = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-t", "--times"))
		{
			times = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-c", "--buffer-count"))
		{
			num_buffers = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-q", "--queue-variant"))
		{
			queue_variant = get_arg(argv, ++i, argc);
			if (queue_variant < 0 || queue_variant > 1)
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
	copying_2();
	return 0;
}
