// Concurrent host and BDA compute writes to independent words in one mapped buffer.

#include "vulkan_common.h"

#include "vulkan_memory_tracking_2.inc"

#include <bit>
#include <mutex>
#include <unistd.h>

static VkDeviceSize buffer_size = 1024 * 1024;
static uint32_t iterations = 16;
static constexpr uint32_t writer_count = 3;
static constexpr uint32_t workgroup_size = 64;
static bool use_fill_buffer = false;
static bool use_host_memory_writes = false;
static VkPhysicalDeviceHostMemoryWriteFeaturesARM host_memory_write_features = {
	VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_MEMORY_WRITE_FEATURES_ARM, nullptr, VK_TRUE
};

static bool check_host_memory_write_features(VkPhysicalDevice physical_device)
{
	VkPhysicalDeviceHostMemoryWriteFeaturesARM supported = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_MEMORY_WRITE_FEATURES_ARM, nullptr, VK_FALSE
	};
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported };
	vkGetPhysicalDeviceFeatures2(physical_device, &features);
	if (!supported.hostMemoryWriteTracking)
	{
		printf("Host memory write tracking is not supported\n");
		return false;
	}
	return true;
}

struct HostMemoryWriteCallbackState
{
	std::mutex mutex;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	uint32_t queueSubmitCallbacks = 0;
	uint32_t unmapCallbacks = 0;
	uint64_t dirtyPages = 0;
};

struct PushConstants
{
	VkDeviceAddress address;
	uint32_t stride;
	uint32_t lane;
	uint32_t word_count;
	uint32_t iterations;
};

static_assert(sizeof(PushConstants) == 24);

static void show_usage()
{
	printf("-b/--buffer-size N  Buffer size in bytes (default %lu)\n", (unsigned long)buffer_size);
	printf("-i/--iterations N   Writes per writer (default %u)\n", iterations);
	printf("-f/--fill-buffer    Use vkCmdFillBuffer instead of compute shaders\n");
	printf("-hmw/--host-memory-writes  Enable VK_ARM_host_memory_writes\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-b", "--buffer-size"))
	{
		buffer_size = get_arg(argv, ++i, argc);
		return buffer_size > 0;
	}
	if (match(argv[i], "-i", "--iterations"))
	{
		iterations = get_arg(argv, ++i, argc);
		return iterations > 0;
	}
	if (match(argv[i], "-f", "--fill-buffer"))
	{
		use_fill_buffer = true;
		reqs.required_queue_flags = VK_QUEUE_TRANSFER_BIT;
		reqs.bufferDeviceAddress = false;
		reqs.reqfeat12.bufferDeviceAddress = VK_FALSE;
		std::erase(reqs.device_extensions, std::string(VK_KHR_MAINTENANCE_6_EXTENSION_NAME));
		return true;
	}
	if (match(argv[i], "-hmw", "--host-memory-writes"))
	{
		if (use_host_memory_writes) return false;
		use_host_memory_writes = true;
		reqs.device_extensions.push_back(VK_ARM_HOST_MEMORY_WRITES_EXTENSION_NAME);
		host_memory_write_features.pNext = reqs.extension_features;
		reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&host_memory_write_features);
		return true;
	}
	return false;
}

static void VKAPI_CALL host_memory_write_callback(VkDevice device, const VkHostMemoryWriteCallbackInfoARM* info)
{
	assert(device != VK_NULL_HANDLE);
	assert(info);
	assert(info->sType == VK_STRUCTURE_TYPE_HOST_MEMORY_WRITE_CALLBACK_INFO_ARM);
	assert(info->pNext == nullptr);
	assert(info->pageSize > 0);
	HostMemoryWriteCallbackState* state = static_cast<HostMemoryWriteCallbackState*>(info->pUserData);
	assert(state);
	assert(info->memory == state->memory);
	std::lock_guard<std::mutex> lock(state->mutex);
	if (info->type == VK_HOST_MEMORY_WRITE_TYPE_QUEUE_SUBMIT_ARM)
	{
		assert(info->maskSize > 0);
		assert(info->pMask);
		state->queueSubmitCallbacks++;
	}
	else
	{
		assert(info->type == VK_HOST_MEMORY_WRITE_TYPE_UNMAP_ARM);
		state->unmapCallbacks++;
	}
	for (uint32_t i = 0; i < info->maskSize; i++) state->dirtyPages += std::popcount(info->pMask[i]);
}

static uint32_t fill_writer_for_word(uint32_t index, uint32_t total_words)
{
	const uint32_t page_words = getpagesize() / sizeof(uint32_t);
	const uint32_t page_start = (index / page_words) * page_words;
	const uint32_t words = std::min(page_words, total_words - page_start);
	const uint32_t region_words = words / writer_count;
	const uint32_t local_index = index - page_start;
	if (local_index < region_words) return 0;
	if (local_index < region_words * 2) return 1;
	return 2;
}

static void record_fill_commands(VkCommandBuffer command, VkBuffer buffer, uint32_t writer, uint32_t total_words)
{
	assert(writer > 0 && writer < writer_count);
	const uint32_t page_words = getpagesize() / sizeof(uint32_t);
	for (uint32_t iteration = 0; iteration < iterations; iteration++)
	{
		for (uint32_t page_start = 0; page_start < total_words; page_start += page_words)
		{
			const uint32_t words = std::min(page_words, total_words - page_start);
			const uint32_t region_words = words / writer_count;
			const uint32_t first = writer == 1 ? region_words : region_words * 2;
			const uint32_t count = writer == 1 ? region_words : words - first;
			if (count == 0) continue;
			vkCmdFillBuffer(command, buffer, (page_start + first) * sizeof(uint32_t),
			                count * sizeof(uint32_t), (writer << 28) | iteration);
		}
		if (iteration + 1 < iterations)
		{
			VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			                     1, &barrier, 0, nullptr, 0, nullptr);
		}
	}
}

static VkShaderModule create_shader(const vulkan_setup_t& vulkan)
{
	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	info.codeSize = vulkan_memory_tracking_2_spirv_len;
	info.pCode = reinterpret_cast<const uint32_t*>(vulkan_memory_tracking_2_spirv);
	VkShaderModule shader = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &info, nullptr, &shader);
	check(result);
	assert(shader_has_device_addresses(std::vector<uint32_t>(info.pCode, info.pCode + info.codeSize / sizeof(uint32_t))));
	return shader;
}

static void push_constants_with_bda_marking(const vulkan_setup_t& vulkan, VkCommandBuffer command_buffer,
                                            VkPipelineLayout layout, const PushConstants& constants)
{
	VkDeviceSize marked_offset = 0;
	VkMarkingTypeARM marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
	VkMarkingSubTypeARM marking_sub_type = { .deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM };
	VkMarkedOffsetsARM markings = { VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr };
	markings.count = 1;
	markings.pOffsets = &marked_offset;
	markings.pMarkingTypes = &marking_type;
	markings.pSubTypes = &marking_sub_type;
#ifdef VULKAN_1_4
	if (vulkan.apiVersion >= VK_API_VERSION_1_4)
	{
		VkPushConstantsInfo push_info = { VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO, nullptr };
		push_info.layout = layout;
		push_info.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		push_info.size = sizeof(constants);
		push_info.pValues = &constants;
		if (vulkan.has_trace_helpers) push_info.pNext = &markings;
		vkCmdPushConstants2(command_buffer, &push_info);
		return;
	}
#endif
	assert(vulkan.vkCmdPushConstants2);
	VkPushConstantsInfoKHR push_info = { VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR, nullptr };
	push_info.layout = layout;
	push_info.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_info.size = sizeof(constants);
	push_info.pValues = &constants;
	if (vulkan.has_trace_helpers) push_info.pNext = &markings;
	vulkan.vkCmdPushConstants2(command_buffer, &push_info);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.queues = 2;
	reqs.required_queue_flags = VK_QUEUE_COMPUTE_BIT;
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
	reqs.device_extensions.push_back("VK_KHR_maintenance6");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_memory_tracking_2", reqs);

	if (use_host_memory_writes && !check_host_memory_write_features(vulkan.physical))
	{
		return 77;
	}

	if (buffer_size < writer_count * sizeof(uint32_t))
	{
		printf("Buffer size must contain at least three words\n");
		test_done(vulkan);
		return 1;
	}
	const uint32_t word_count = static_cast<uint32_t>(buffer_size / (writer_count * sizeof(uint32_t)));
	assert(word_count > 0);

	VkQueue queues[2] = {};
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queues[0]);
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 1, &queues[1]);
	assert(queues[0] != VK_NULL_HANDLE);
	assert(queues[1] != VK_NULL_HANDLE);

	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = buffer_size;
	buffer_info.usage = use_fill_buffer ? VK_BUFFER_USAGE_TRANSFER_DST_BIT :
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, "memory_tracking_2_target");

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, buffer, &requirements);
	VkMemoryAllocateFlagsInfo flags_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr };
	flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, use_fill_buffer ? nullptr : &flags_info };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits,
	                                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	HostMemoryWriteCallbackState callback_state;
	VkHostMemoryWriteCallbackARM callback_info = {
		VK_STRUCTURE_TYPE_HOST_MEMORY_WRITE_CALLBACK_ARM,
		allocate_info.pNext,
		0,
		host_memory_write_callback,
		&callback_state
	};
	if (use_host_memory_writes) allocate_info.pNext = &callback_info;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &memory);
	check(result);
	callback_state.memory = memory;
	result = vkBindBufferMemory(vulkan.device, buffer, memory, 0);
	check(result);

	uint32_t* mapped = nullptr;
	result = vkMapMemory(vulkan.device, memory, 0, buffer_size, 0, reinterpret_cast<void**>(&mapped));
	check(result);
	memset(mapped, 0, buffer_size);

	PushConstants constants = { 0, writer_count, 0, word_count, iterations };
	if (!use_fill_buffer)
	{
		VkBufferDeviceAddressInfo address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
		address_info.buffer = buffer;
		constants.address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
		assert(constants.address != 0);
	}

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &pool);
	check(result);
	VkCommandBuffer commands[2] = {};
	VkCommandBufferAllocateInfo command_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_info.commandPool = pool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = 2;
	result = vkAllocateCommandBuffers(vulkan.device, &command_info, commands);
	check(result);

	VkShaderModule shader = VK_NULL_HANDLE;
	VkPushConstantRange push_range = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants) };
	VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges = &push_range;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = shader;
	stage.pName = "main";
	VkComputePipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	pipeline_info.stage = stage;
	pipeline_info.layout = layout;
	VkPipeline pipeline = VK_NULL_HANDLE;
	if (!use_fill_buffer)
	{
		shader = create_shader(vulkan);
		result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &layout);
		check(result);
		pipeline_info.stage.module = shader;
		pipeline_info.layout = layout;
		result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
		check(result);
	}

	for (uint32_t writer = 0; writer < 2; writer++)
	{
		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(commands[writer], &begin_info);
		check(result);
		if (use_fill_buffer)
		{
			record_fill_commands(commands[writer], buffer, writer + 1, static_cast<uint32_t>(buffer_size / sizeof(uint32_t)));
		}
		else
		{
			constants.lane = writer + 1;
			vkCmdBindPipeline(commands[writer], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			push_constants_with_bda_marking(vulkan, commands[writer], layout, constants);
			vkCmdDispatch(commands[writer], (word_count + workgroup_size - 1) / workgroup_size, 1, 1);
		}
		VkMemoryBarrier host_read_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
		host_read_barrier.srcAccessMask = use_fill_buffer ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT;
		host_read_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		vkCmdPipelineBarrier(commands[writer],
		                     use_fill_buffer ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		                     VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_read_barrier, 0, nullptr, 0, nullptr);
		result = vkEndCommandBuffer(commands[writer]);
		check(result);
	}

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fences[2] = {};
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fences[0]);
	check(result);
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fences[1]);
	check(result);

	bench_start_iteration(vulkan.bench);
	for (uint32_t writer = 0; writer < 2; writer++)
	{
		VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &commands[writer];
		result = vkQueueSubmit(queues[writer], 1, &submit_info, fences[writer]);
		check(result);
	}
	for (uint32_t iteration = 0; iteration < iterations; iteration++)
	{
		if (use_fill_buffer)
		{
			const uint32_t total_words = static_cast<uint32_t>(buffer_size / sizeof(uint32_t));
			for (uint32_t index = 0; index < total_words; index++)
			{
				if (fill_writer_for_word(index, total_words) == 0) mapped[index] = iteration;
			}
		}
		else
		{
			for (uint32_t index = 0; index < word_count; index++) mapped[index * writer_count] = iteration;
		}
	}
	result = vkWaitForFences(vulkan.device, 2, fences, VK_TRUE, UINT64_MAX);
	check(result);
	if (vulkan.has_explicit_host_updates)
	{
		testFlushMemory(vulkan, memory, 0, buffer_size, true);
	}
	// Submit the completed host and device writes as a normal buffer use before asserting them.
	testQueueBuffer(vulkan, queues[0], { buffer });
	bench_stop_iteration(vulkan.bench);
	if (vulkan.vkAssertBuffer)
	{
		uint32_t checksum = 0;
		VkUpdateBufferInfoARM assert_info = { VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffer, 0, buffer_size, nullptr };
		result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum, "memory tracking 2 final writers");
		check(result);
	}

	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		if (use_fill_buffer)
		{
			const uint32_t total_words = static_cast<uint32_t>(buffer_size / sizeof(uint32_t));
			for (uint32_t index = 0; index < total_words; index++)
			{
				const uint32_t writer = fill_writer_for_word(index, total_words);
				const uint32_t expected = (writer << 28) | (iterations - 1);
				const uint32_t actual = mapped[index];
				if (actual != expected) ABORT("Fatal error: Word %u for writer %u was 0x%08x, expected 0x%08x", index, writer, actual, expected);
			}
		}
		else
		{
			for (uint32_t index = 0; index < word_count; index++)
			{
				for (uint32_t writer = 0; writer < writer_count; writer++)
				{
					const uint32_t expected = (writer << 28) | (iterations - 1);
					const uint32_t actual = mapped[index * writer_count + writer];
					if (actual != expected) ABORT("Fatal error: Word %u for writer %u was 0x%08x, expected 0x%08x", index, writer, actual, expected);
				}
			}
		}
	}

	vkDestroyFence(vulkan.device, fences[1], nullptr);
	vkDestroyFence(vulkan.device, fences[0], nullptr);
	if (pipeline) vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	if (layout) vkDestroyPipelineLayout(vulkan.device, layout, nullptr);
	if (shader) vkDestroyShaderModule(vulkan.device, shader, nullptr);
	vkDestroyCommandPool(vulkan.device, pool, nullptr);
	vkUnmapMemory(vulkan.device, memory);
	if (use_host_memory_writes)
	{
		std::lock_guard<std::mutex> lock(callback_state.mutex);
		assert(callback_state.queueSubmitCallbacks >= 2);
		assert(callback_state.unmapCallbacks == 1);
		assert(callback_state.dirtyPages > 0);
	}
	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	testFreeMemory(vulkan, memory);
	test_done(vulkan);
	return 0;
}
