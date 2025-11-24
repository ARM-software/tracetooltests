// Test of basic memory aliasing with two overlapping buffers.

#include "vulkan_common.h"

static int flush_variant = 0;

static void show_usage()
{
	printf("-F/--flush-variant N   Set memory flush variant (default %d)\n", flush_variant);
	printf("\t0 - use coherent memory, no explicit flushing\n");
	printf("\t1 - use any memory, explicit flushing\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-F", "--flush-variant"))
	{
		flush_variant = get_arg(argv, ++i, argc);
		return (flush_variant >= 0 && flush_variant <= 1);
	}
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_aliasing_3", reqs);
	VkBuffer parent;
	VkBuffer child;
	VkQueue queue;
	uint32_t orig_crc_parent = 0;
	uint32_t orig_crc_child = 0;

	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = 1024;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &parent);
	assert(result == VK_SUCCESS);
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &child);
	assert(result == VK_SUCCESS);

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan.device, parent, &memory_requirements);
	VkMemoryPropertyFlagBits memoryflags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	if (flush_variant == 0)
	{
		memoryflags = (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	}
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, memoryflags);
	const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
	const uint32_t aligned_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_size;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(result);
	assert(memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, parent, memory, 0);
	check(result);
	result = vkBindBufferMemory(vulkan.device, child, memory, 0);
	check(result);

	char* data = nullptr;
	result = vkMapMemory(vulkan.device, memory, 0, 1024, 0, (void**)&data);
	assert(result == VK_SUCCESS);
	memset(data, 0xdeadfeed, 1024);
	orig_crc_parent = adler32((unsigned char*)data, 1024);
	if (flush_variant == 1 || vulkan.has_explicit_host_updates) testFlushMemory(vulkan, memory, 0, 1024, flush_variant != 1);
	vkUnmapMemory(vulkan.device, memory);

	// make sure we submit all of them somewhere and add proper memory barriers
	testQueueBuffer(vulkan, queue, { parent, child });

	if (vulkan.vkAssertBuffer)
	{
		const uint32_t parent_crc = vulkan.vkAssertBuffer(vulkan.device, parent, 0, VK_WHOLE_SIZE, "parent buffer");
		assert(parent_crc == orig_crc_parent);
		(void)parent_crc;
		const uint32_t child_crc = vulkan.vkAssertBuffer(vulkan.device, child, 0, VK_WHOLE_SIZE, "child buffer");
		assert(child_crc == orig_crc_child);
		(void)child_crc;
	}

	testQueueBuffer(vulkan, queue, { parent, child });

	vkDestroyBuffer(vulkan.device, parent, nullptr);
	vkDestroyBuffer(vulkan.device, child, nullptr);
	testFreeMemory(vulkan, memory);
	test_done(vulkan);
	return 0;
}
