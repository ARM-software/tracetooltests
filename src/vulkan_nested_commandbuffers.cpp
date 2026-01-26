#include "vulkan_common.h"

struct buffer_allocation_t
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)i;
	(void)argc;
	(void)argv;
	(void)reqs;
	return false;
}

static buffer_allocation_t create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage, const char* name)
{
	buffer_allocation_t out{};
	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	info.size = size;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &info, nullptr, &out.buffer);
	check(result);

	VkMemoryRequirements memreq = {};
	vkGetBufferMemoryRequirements(vulkan.device, out.buffer, &memreq);

	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	alloc.allocationSize = memreq.size;
	alloc.memoryTypeIndex = get_device_memory_type(memreq.memoryTypeBits,
	                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	result = vkAllocateMemory(vulkan.device, &alloc, nullptr, &out.memory);
	check(result);

	result = vkBindBufferMemory(vulkan.device, out.buffer, out.memory, 0);
	check(result);

	if (name)
	{
		test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)out.buffer, name);
	}
	return out;
}

static void destroy_buffer(const vulkan_setup_t& vulkan, buffer_allocation_t& buf)
{
	if (buf.buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan.device, buf.buffer, nullptr);
		buf.buffer = VK_NULL_HANDLE;
	}
	if (buf.memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(vulkan.device, buf.memory, nullptr);
		buf.memory = VK_NULL_HANDLE;
	}
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	VkPhysicalDeviceNestedCommandBufferFeaturesEXT nested_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_FEATURES_EXT,
		nullptr,
		VK_TRUE,
		VK_FALSE,
		VK_FALSE
	};
	reqs.device_extensions.push_back(VK_EXT_NESTED_COMMAND_BUFFER_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&nested_features);
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_nested_commandbuffers", reqs);

	VkPhysicalDeviceNestedCommandBufferPropertiesEXT nested_props = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_PROPERTIES_EXT,
		nullptr,
		0
	};
	VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &nested_props };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &props);
	if (nested_props.maxCommandBufferNestingLevel < 2)
	{
		printf("Nested command buffer nesting level too small (%u)\n", nested_props.maxCommandBufferNestingLevel);
		test_done(vulkan);
		return 77;
	}

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	const VkDeviceSize buffer_size = 4096;
	buffer_allocation_t src = create_buffer(vulkan, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "nested_src");
	buffer_allocation_t mid = create_buffer(vulkan, buffer_size,
	                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                        "nested_mid");
	buffer_allocation_t dst = create_buffer(vulkan, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "nested_dst");

	std::vector<unsigned char> src_data(buffer_size);
	for (size_t i = 0; i < src_data.size(); i++)
	{
		src_data[i] = static_cast<unsigned char>((i * 13) & 0xff);
	}
	uint32_t expected_crc = adler32(src_data.data(), src_data.size());

	void* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, src.memory, 0, buffer_size, 0, &mapped);
	check(result);
	memcpy(mapped, src_data.data(), src_data.size());
	testFlushMemory(vulkan, src.memory, 0, buffer_size);
	vkUnmapMemory(vulkan.device, src.memory);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = 0;
	VkCommandPool pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &pool);
	check(result);

	VkCommandBufferAllocateInfo alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	alloc.commandPool = pool;
	alloc.commandBufferCount = 1;

	VkCommandBuffer primary = VK_NULL_HANDLE;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	result = vkAllocateCommandBuffers(vulkan.device, &alloc, &primary);
	check(result);

	VkCommandBuffer secondary_parent = VK_NULL_HANDLE;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
	result = vkAllocateCommandBuffers(vulkan.device, &alloc, &secondary_parent);
	check(result);

	VkCommandBuffer secondary_child = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &alloc, &secondary_child);
	check(result);

	VkCommandBufferInheritanceInfo inherit = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, nullptr };
	VkCommandBufferBeginInfo secondary_begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	secondary_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	secondary_begin.pInheritanceInfo = &inherit;

	VkBufferCopy copy_region = { 0, 0, buffer_size };

	result = vkBeginCommandBuffer(secondary_child, &secondary_begin);
	check(result);
	vkCmdCopyBuffer(secondary_child, src.buffer, mid.buffer, 1, &copy_region);
	result = vkEndCommandBuffer(secondary_child);
	check(result);

	result = vkBeginCommandBuffer(secondary_parent, &secondary_begin);
	check(result);
	vkCmdExecuteCommands(secondary_parent, 1, &secondary_child);
	vkCmdCopyBuffer(secondary_parent, mid.buffer, dst.buffer, 1, &copy_region);
	result = vkEndCommandBuffer(secondary_parent);
	check(result);

	VkCommandBufferBeginInfo primary_begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	primary_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(primary, &primary_begin);
	check(result);
	vkCmdExecuteCommands(primary, 1, &secondary_parent);
	result = vkEndCommandBuffer(primary);
	check(result);

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fence = VK_NULL_HANDLE;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);

	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &primary;
	bench_start_iteration(vulkan.bench);
	result = vkQueueSubmit(queue, 1, &submit, fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
	bench_stop_iteration(vulkan.bench);

	if (vulkan.vkAssertBuffer)
	{
		test_marker(vulkan, "Injecting buffer assertions");
		uint32_t mid_crc = 0;
		result = vulkan.vkAssertBuffer(vulkan.device, mid.buffer, 0, VK_WHOLE_SIZE, &mid_crc, "nested mid buffer");
		check(result);
		assert(mid_crc == expected_crc);
		(void)mid_crc;
		uint32_t dst_crc = 0;
		result = vulkan.vkAssertBuffer(vulkan.device, dst.buffer, 0, VK_WHOLE_SIZE, &dst_crc, "nested dst buffer");
		check(result);
		assert(dst_crc == expected_crc);
		(void)dst_crc;
	}

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, pool, 1, &primary);
	vkFreeCommandBuffers(vulkan.device, pool, 1, &secondary_parent);
	vkFreeCommandBuffers(vulkan.device, pool, 1, &secondary_child);
	vkDestroyCommandPool(vulkan.device, pool, nullptr);
	destroy_buffer(vulkan, src);
	destroy_buffer(vulkan, mid);
	destroy_buffer(vulkan, dst);
	test_done(vulkan);
	return 0;
}
