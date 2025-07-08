// Unit test to try out various combinations of ping-ponging jobs between two queues with dependencies.

#include "vulkan_common.h"

static int fence_variant = 0;
static int flush_variant = 0;
static int map_variant = 0;
static int queue_variant = 0;
static unsigned buffer_size = (32 * 1024);
static unsigned num_buffers = 10;
static PFN_vkQueueSubmit2 fpQueueSubmit2 = nullptr;

static void show_usage()
{
	printf("-b/--buffer-size N     Set buffer size (default %u)\n", buffer_size);
	printf("-c/--buffer-count N    Set buffer count (default %u)\n", num_buffers);
	printf("-t/--times N           Times to repeat\n");
	printf("-q/--queue-variant N   Set queue variant (default %d)\n", queue_variant);
	printf("\t0 - ping-pong between two queues\n");
	printf("\t1 - put all jobs on one queue\n");
	printf("-f/--fence-variant N   Set fence variant (default %d)\n", fence_variant);
	printf("\t0 - wait for fences each loop\n");
	printf("-F/--flush-variant N   Set memory flush variant (default %d)\n", flush_variant);
	printf("\t0 - use coherent memory, no explicit flushing\n");
	printf("\t1 - use any memory, explicit flushing\n");
	printf("-m/--map-variant N     Set map variant (default %d)\n", map_variant);
	printf("\t0 - memory map kept open\n");
	printf("\t1 - memory map unmapped before submit\n");
	printf("\t2 - memory map remapped to tiny area before submit\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-b", "--buffer-size"))
	{
		buffer_size = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-t", "--times"))
	{
		p__loops = get_arg(argv, ++i, argc);
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
		if (queue_variant == 1) reqs.queues = 1;
		return (queue_variant >= 0 && queue_variant <= 1);
	}
	else if (match(argv[i], "-m", "--map-variant"))
	{
		map_variant = get_arg(argv, ++i, argc);
		return (map_variant >= 0 && map_variant <= 2);
	}
	else if (match(argv[i], "-f", "--fence-variant"))
	{
		fence_variant = get_arg(argv, ++i, argc);
		return (fence_variant == 0);
	}
	else if (match(argv[i], "-F", "--flush-variant"))
	{
		flush_variant = get_arg(argv, ++i, argc);
		return (flush_variant >= 0 && flush_variant <= 1);
	}
	return false;
}

static void copying_2(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.queues = 2;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_copying_2", reqs);
	VkResult result;

	if (reqs.apiVersion >= VK_API_VERSION_1_3)
	{
		fpQueueSubmit2 = (PFN_vkQueueSubmit2)vkGetDeviceProcAddr(vulkan.device, "vkQueueSubmit2");
		assert(fpQueueSubmit2);
	}

	VkQueue queue1;
	VkQueue queue2;
	VkDeviceQueueInfo2 qinfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2, nullptr, 0, 0, 0 };
	vkGetDeviceQueue2(vulkan.device, &qinfo, &queue1);
	qinfo.queueIndex = (queue_variant == 0) ? 1 : 0;
	vkGetDeviceQueue2(vulkan.device, &qinfo, &queue2);

	std::vector<VkBuffer> origin_buffers(num_buffers);
	std::vector<VkBuffer> target_buffers(num_buffers);
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vulkan.garbage_pointers) bufferCreateInfo.pQueueFamilyIndices = (const uint32_t*)0xdeadbeef;
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
	VkMemoryPropertyFlagBits memoryflags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	if (flush_variant == 0)
	{
		memoryflags = (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	}
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, memoryflags);
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

	testBindBufferMemory(vulkan, origin_buffers, origin_memory, aligned_size);
	testBindBufferMemory(vulkan, target_buffers, target_memory, aligned_size);

	char* data = nullptr;
	result = vkMapMemory(vulkan.device, origin_memory, 0, num_buffers * aligned_size, 0, (void**)&data);
	assert(result == VK_SUCCESS);
	VkDeviceSize offset = 0;
	for (unsigned i = 0; i < num_buffers; i++)
	{
		memset(data + offset, i, aligned_size);
		offset += aligned_size;
	}
	if (flush_variant == 1 || vulkan.has_explicit_host_updates) testFlushMemory(vulkan, origin_memory, 0, VK_WHOLE_SIZE, flush_variant != 1);
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
	command_buffer_begin_info.flags = 0;
	std::vector<VkSemaphore> semaphores(num_buffers);
	std::vector<VkFence> fences(num_buffers);
	for (unsigned i = 0; i < num_buffers; i++)
	{
		result = vkBeginCommandBuffer(command_buffers[i], &command_buffer_begin_info);
		check(result);
		testCmdCopyBuffer(vulkan, command_buffers.at(i), std::vector<VkBuffer>{ origin_buffers.at(i) }, std::vector<VkBuffer>{ target_buffers.at(i) }, buffer_size);
		result = vkEndCommandBuffer(command_buffers[i]);
		check(result);

		VkSemaphoreCreateInfo seminfo = {};
		seminfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		seminfo.pNext = nullptr;
		seminfo.flags = 0;
		result = vkCreateSemaphore(vulkan.device, &seminfo, nullptr, &semaphores.at(i));
		check(result);

		VkFenceCreateInfo fence_create_info = {};
		fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		result = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fences[i]);
		check(result);
	}

	for (int frame = 0; frame < p__loops; frame++)
	{
		bench_start_iteration(vulkan.bench);
		for (unsigned i = 0; i < num_buffers; i++)
		{
			if (flush_variant == 1 || vulkan.has_explicit_host_updates) testFlushMemory(vulkan, origin_memory, aligned_size * i, aligned_size, flush_variant != 1); // add useless flush
			VkPipelineStageFlags flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
			VkQueue q = queue1;
			if (queue_variant == 0 && i % 2 == 1) q = queue2; // interleave mode
			VkFrameBoundaryEXT fbe = { VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT, nullptr };
			fbe.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
			fbe.frameID = frame;
			if (reqs.apiVersion < VK_API_VERSION_1_3)
			{
				VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
				submit_info.commandBufferCount = 1;
				submit_info.pCommandBuffers = &command_buffers[i];
				if (i != num_buffers - 1)
				{
					submit_info.signalSemaphoreCount = 1;
					submit_info.pSignalSemaphores = &semaphores[i];
				}
				if (i > 0)
				{
					submit_info.waitSemaphoreCount = 1;
					submit_info.pWaitSemaphores = &semaphores.at(i - 1);
					submit_info.pWaitDstStageMask = &flags;
				}
				if (i == num_buffers - 1 && vulkan.device_extensions.count(VK_EXT_FRAME_BOUNDARY_EXTENSION_NAME))
				{
					submit_info.pNext = &fbe;
				}
				result = vkQueueSubmit(q, 1, &submit_info, fences[i]);
				check(result);
			}
			else
			{
				VkSemaphore waitsema = (i > 0) ? semaphores.at(i - 1) : VK_NULL_HANDLE;
				VkSemaphore signalsema = (i != num_buffers - 1) ? semaphores.at(i) : VK_NULL_HANDLE;
				VkSemaphoreSubmitInfo s1 = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, waitsema, VK_PIPELINE_STAGE_2_COPY_BIT, 0 }; // wait semaphore
				VkSemaphoreSubmitInfo s2 = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, signalsema, VK_PIPELINE_STAGE_2_COPY_BIT, 0 }; // signal semaphore
				VkCommandBufferSubmitInfo csi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, command_buffers[i], 0 };
				VkSubmitInfo2 submit_info2 = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, (i > 0), &s1, 1, &csi, (i != num_buffers - 1), &s2 };
				if (i == num_buffers - 1 && vulkan.device_extensions.count(VK_EXT_FRAME_BOUNDARY_EXTENSION_NAME))
				{
					submit_info2.pNext = &fbe;
				}
				result = fpQueueSubmit2(q, 1, &submit_info2, fences[i]);
				check(result);
			}
		}
		if (fence_variant == 0)
		{
			result = vkWaitForFences(vulkan.device, num_buffers, fences.data(), VK_TRUE, UINT64_MAX);
			check(result);
			result = vkResetFences(vulkan.device, num_buffers, fences.data());
			check(result);
		}
		bench_stop_iteration(vulkan.bench);
	}

	// Verification
	if (vulkan.vkAssertBuffer)
	{
		for (unsigned i = 0; i < num_buffers; i++)
		{
			const uint32_t orig = vulkan.vkAssertBuffer(vulkan.device, origin_buffers.at(i), 0, VK_WHOLE_SIZE, "origin buffer");
			const uint32_t dest = vulkan.vkAssertBuffer(vulkan.device, target_buffers.at(i), 0, VK_WHOLE_SIZE, "destination buffer");
			assert(orig == dest);
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
	testFreeMemory(vulkan, origin_memory);
	testFreeMemory(vulkan, target_memory);
	vkFreeCommandBuffers(vulkan.device, command_pool, num_buffers, command_buffers.data());
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	test_done(vulkan);
}

int main(int argc, char** argv)
{
	copying_2(argc, argv);
	return 0;
}
