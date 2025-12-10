// Test of memory aliasing with buffers. This test creates 2 buffers that only partially overlap, without being within a bigger buffer.
// View of memory 0----512----1024----1536----2048
// child_1        ++++++++++++++------------------
// child_2        ------++++++++++++++++++++++++++

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
	reqs.queues = 2;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_aliasing_2", reqs);
	VkBuffer fake_parent;
	VkBuffer child_1;
	VkBuffer child_2;
	VkQueue queue;
	uint32_t orig_crc_child_1 = 0;
	uint32_t orig_crc_child_2 = 0;
	uint32_t latest_crc_child_1 = 0;

	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = 2048;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &fake_parent);
	assert(result == VK_SUCCESS);
	bufferCreateInfo.size = 1024;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &child_1);
	assert(result == VK_SUCCESS);
	bufferCreateInfo.size = 1536;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &child_2);
	assert(result == VK_SUCCESS);

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan.device, fake_parent, &memory_requirements);
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

	result = vkBindBufferMemory(vulkan.device, child_1, memory, 0);
	check(result);
	result = vkBindBufferMemory(vulkan.device, child_2, memory, 512);
	check(result);

	char* data = nullptr;
	result = vkMapMemory(vulkan.device, memory, 0, 1024, 0, (void**)&data);
	assert(result == VK_SUCCESS);
	memset(data, 0xdeadfeed, 1024);
	orig_crc_child_1 = adler32((unsigned char*)data, 1024);
	if (flush_variant == 1 || vulkan.has_explicit_host_updates) testFlushMemory(vulkan, memory, 0, 1024, flush_variant != 1);
	vkUnmapMemory(vulkan.device, memory);

	result = vkMapMemory(vulkan.device, memory, 512, 1536, 0, (void**)&data);
	assert(result == VK_SUCCESS);
	memset(data, 0xabcdabcd, 1536);
	orig_crc_child_2 = adler32((unsigned char*)data, 1536);
	if (flush_variant == 1 || vulkan.has_explicit_host_updates) testFlushMemory(vulkan, memory, 512, 1536, flush_variant != 1);
	vkUnmapMemory(vulkan.device, memory);

	// remap child_1 buffer to see if overlapping buffer child_2 has modified it
	result = vkMapMemory(vulkan.device, memory, 0, 1024, 0, (void**)&data);
	assert(result == VK_SUCCESS);
	latest_crc_child_1 = adler32((unsigned char*)data, 1024);
	vkUnmapMemory(vulkan.device, memory);

	assert(latest_crc_child_1 != orig_crc_child_1);

	if (vulkan.vkAssertBuffer)
	{
		uint32_t crc_child_1 = 0;
		uint32_t crc_child_2 = 0;
		result = vulkan.vkAssertBuffer(vulkan.device, child_1, 0, VK_WHOLE_SIZE, &crc_child_1, "child_1 buffer");
		assert(result == VK_SUCCESS);
		assert(crc_child_1 != orig_crc_child_1);
		assert(crc_child_1 == latest_crc_child_1);
		(void)crc_child_1;
		(void)result;
		result = vulkan.vkAssertBuffer(vulkan.device, child_2, 0, VK_WHOLE_SIZE, &crc_child_2, "child_2 buffer");
		assert(result == VK_SUCCESS);
		assert(crc_child_2 == orig_crc_child_2);
		(void)crc_child_2;
	}

	testQueueBuffer(vulkan, queue, { child_1, child_2 });

	vkDestroyBuffer(vulkan.device, fake_parent, nullptr);
	vkDestroyBuffer(vulkan.device, child_1, nullptr);
	vkDestroyBuffer(vulkan.device, child_2, nullptr);
	testFreeMemory(vulkan, memory);
	test_done(vulkan);
	return 0;
}
