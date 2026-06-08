#include "vulkan_common.h"

static int buffer_size = 1024 * 1024;
static int num_untouched_buffers = 10;
static int num_unused_buffers = 9;
static int num_changed_buffers = 1;
static int memory_type_variant = 0;

static VkMemoryPropertyFlags memory_required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
static VkMemoryPropertyFlags memory_forbidden_flags = 0;

static const char* memory_type_name(int variant)
{
	switch (variant)
	{
	case 0: return "host-coherent";
	case 1: return "host-cached";
	case 2: return "host-cached-coherent";
	case 3: return "host-cached-noncoherent";
	case 4: return "host-visible";
	default: return "unknown";
	}
}

static bool set_memory_type_variant(int variant)
{
	memory_type_variant = variant;
	memory_forbidden_flags = 0;

	switch (variant)
	{
	case 0:
		memory_required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		return true;
	case 1:
		memory_required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		return true;
	case 2:
		memory_required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		return true;
	case 3:
		memory_required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		memory_forbidden_flags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		return true;
	case 4:
		memory_required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		return true;
	default:
		return false;
	}
}

static void show_usage()
{
	printf("-b/--buffer-size N       Set buffer size in bytes (default %d)\n", buffer_size);
	printf("-c/--changed-buffers N   Set written and submitted buffer count (default %d)\n", num_changed_buffers);
	printf("-u/--unused-buffers N    Set written but unsubmitted buffer count (default %d)\n", num_unused_buffers);
	printf("-U/--untouched-buffers N Set mapped but unwritten buffer count (default %d)\n", num_untouched_buffers);
	printf("-m/--memory-type N       Set memory type variant (default %d: %s)\n", memory_type_variant, memory_type_name(memory_type_variant));
	printf("\t0 - host-coherent:        HOST_VISIBLE | HOST_COHERENT\n");
	printf("\t1 - host-cached:          HOST_VISIBLE | HOST_CACHED\n");
	printf("\t2 - host-cached-coherent: HOST_VISIBLE | HOST_CACHED | HOST_COHERENT\n");
	printf("\t3 - host-cached-noncoherent: HOST_VISIBLE | HOST_CACHED without HOST_COHERENT\n");
	printf("\t4 - host-visible:         HOST_VISIBLE\n");
	printf("-t/--times N             Times to repeat (default %d)\n", (int)p__loops);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-b", "--buffer-size"))
	{
		buffer_size = get_arg(argv, ++i, argc);
		return buffer_size > 0;
	}
	else if (match(argv[i], "-c", "--changed-buffers"))
	{
		num_changed_buffers = get_arg(argv, ++i, argc);
		return num_changed_buffers > 0;
	}
	else if (match(argv[i], "-u", "--unused-buffers"))
	{
		num_unused_buffers = get_arg(argv, ++i, argc);
		return num_unused_buffers >= 0;
	}
	else if (match(argv[i], "-U", "--untouched-buffers"))
	{
		num_untouched_buffers = get_arg(argv, ++i, argc);
		return num_untouched_buffers >= 0;
	}
	else if (match(argv[i], "-m", "--memory-type"))
	{
		return set_memory_type_variant(get_arg(argv, ++i, argc));
	}
	else if (match(argv[i], "-t", "--times"))
	{
		p__loops = get_arg(argv, ++i, argc);
		return p__loops > 0;
	}
	return false;
}

static bool find_memory_type(const vulkan_setup_t& vulkan, uint32_t type_filter, uint32_t* index, VkMemoryPropertyFlags* flags)
{
	VkPhysicalDeviceMemoryProperties properties = {};
	vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &properties);

	for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
	{
		if ((type_filter & (1u << i)) == 0) continue;

		const VkMemoryPropertyFlags candidate_flags = properties.memoryTypes[i].propertyFlags;
		if ((candidate_flags & memory_required_flags) == memory_required_flags && (candidate_flags & memory_forbidden_flags) == 0)
		{
			*index = i;
			*flags = candidate_flags;
			return true;
		}
	}

	return false;
}

static void flush_memory_range(const vulkan_setup_t& vulkan, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, bool informative)
{
	testFlushMemory(vulkan, memory, offset, size, informative && vulkan.has_explicit_host_updates);
}

static bool allocate_buffer_memory(const vulkan_setup_t& vulkan, const std::vector<VkBuffer>& buffers, std::vector<VkDeviceMemory>& memory, uint32_t* aligned_size, VkMemoryPropertyFlags* selected_flags)
{
	for (unsigned i = 0; i < buffers.size(); i++)
	{
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan.device, buffers.at(i), &memory_requirements);

		uint32_t memory_type_index = 0;
		VkMemoryPropertyFlags memory_flags = 0;
		if (!find_memory_type(vulkan, memory_requirements.memoryTypeBits, &memory_type_index, &memory_flags))
		{
			printf("Skipping: no memory type for variant %d (%s), required flags 0x%x, forbidden flags 0x%x\n",
			       memory_type_variant, memory_type_name(memory_type_variant), memory_required_flags, memory_forbidden_flags);
			return false;
		}

		const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
		const uint32_t new_aligned_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);
		assert(i == 0 || new_aligned_size == *aligned_size);
		*aligned_size = new_aligned_size;
		*selected_flags = memory_flags;

		VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
		allocate_info.memoryTypeIndex = memory_type_index;
		allocate_info.allocationSize = *aligned_size;

		memory.push_back(VK_NULL_HANDLE);
		VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &memory.back());
		check(result);
		assert(memory.back() != VK_NULL_HANDLE);

		result = vkBindBufferMemory(vulkan.device, buffers.at(i), memory.back(), 0);
		check(result);

		uint8_t* data = nullptr;
		result = vkMapMemory(vulkan.device, memory.back(), 0, *aligned_size, 0, (void**)&data);
		check(result);
		memset(data, i, *aligned_size);
		if ((memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0 || vulkan.has_explicit_host_updates)
		{
			flush_memory_range(vulkan, memory.back(), 0, VK_WHOLE_SIZE, true);
		}
		vkUnmapMemory(vulkan.device, memory.back());

		std::string bufname = std::string("buffer_") + std::to_string(i);
		test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffers.at(i), bufname.c_str());
	}

	return true;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_memory_tracking_1", reqs);
	VkResult result;
	const int num_buffers = num_untouched_buffers + num_unused_buffers + num_changed_buffers;

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	std::vector<VkBuffer> buffers(num_buffers);
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	for (unsigned i = 0; i < buffers.size(); i++)
	{
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffers.at(i));
		assert(result == VK_SUCCESS);
		assert(buffers.at(i) != VK_NULL_HANDLE);
	}

	std::vector<VkDeviceMemory> memory;
	memory.reserve(num_buffers);
	uint32_t aligned_size = 0;
	VkMemoryPropertyFlags selected_memory_flags = 0;
	if (!allocate_buffer_memory(vulkan, buffers, memory, &aligned_size, &selected_memory_flags))
	{
		for (unsigned i = 0; i < memory.size(); i++) testFreeMemory(vulkan, memory[i]);
		for (unsigned i = 0; i < buffers.size(); i++) vkDestroyBuffer(vulkan.device, buffers.at(i), nullptr);
		test_done(vulkan);
		return 77;
	}

	printf("Using memory type variant %d (%s), selected memory flags 0x%x, buffer size %d, changed %d, unused %d, untouched %d\n",
	       memory_type_variant, memory_type_name(memory_type_variant), selected_memory_flags, buffer_size, num_changed_buffers, num_unused_buffers, num_untouched_buffers);

	std::vector<char*> pointers(num_buffers);
	for (unsigned i = 0; i < buffers.size(); i++)
	{
		result = vkMapMemory(vulkan.device, memory[i], 0, aligned_size, 0, (void**)&pointers[i]);
		assert(result == VK_SUCCESS);
	}

	for (unsigned i = 0; i < p__loops; i++)
	{
		bench_start_iteration(vulkan.bench);
		for (int j = 0; j < num_changed_buffers + num_unused_buffers; j++)
		{
			memset(pointers[j], i, buffer_size);
		}
		if ((selected_memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0 || vulkan.has_explicit_host_updates)
		{
			const VkDeviceSize flush_size = (selected_memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0 ? VK_WHOLE_SIZE : buffer_size;
			for (int j = 0; j < num_changed_buffers; j++) flush_memory_range(vulkan, memory[j], 0, flush_size, true);
		}

		std::vector<VkBuffer> submitted_buffers;
		submitted_buffers.reserve(num_changed_buffers);
		for (int j = 0; j < num_changed_buffers; j++) submitted_buffers.push_back(buffers.at(j));
		testQueueBuffer(vulkan, queue, submitted_buffers);
		bench_stop_iteration(vulkan.bench);
	}

	// Cleanup...
	for (unsigned i = 0; i < memory.size(); i++) vkUnmapMemory(vulkan.device, memory[i]);
	for (unsigned i = 0; i < buffers.size(); i++) vkDestroyBuffer(vulkan.device, buffers.at(i), nullptr);
	for (unsigned i = 0; i < memory.size(); i++) testFreeMemory(vulkan, memory[i]);
	test_done(vulkan);
	return 0;
}
