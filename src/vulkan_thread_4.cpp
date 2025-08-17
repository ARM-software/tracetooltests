// Test for multi-threaded tracers. Test concurrent resource creation and dependencies.

#include <condition_variable>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_set>

#include "vulkan_common.h"

#define MAX_BUFFERS 100
#define BUFFER_SIZE 100

static vulkan_setup_t vulkan;
static VkCommandPool pool;
static VkCommandBuffer cmd;
static std::atomic<VkBuffer> buffers[MAX_BUFFERS];
static VkDeviceMemory memory;
static std::atomic_int next_buffer = 0;
static int sleep_time = 10;
static VkDeviceSize offsets[MAX_BUFFERS];
static int aligned_buffer_size = 0;

static void dummy_vkCmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(buffer != VK_NULL_HANDLE);
	vkCmdBindIndexBuffer(cmd, buffer, 0, VK_INDEX_TYPE_UINT32);
}

static void dummy_vkCmdBindVertexBuffers(VkCommandBuffer cmd, const std::vector<VkBuffer>& buffers)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(buffers.size() > 0);
	assert(buffers[0] != VK_NULL_HANDLE);
	vkCmdBindVertexBuffers(cmd, 0, buffers.size(), buffers.data(), offsets);
}

static void dummy_vkCmdFillBuffer(VkCommandBuffer cmd, VkBuffer buffer)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(buffer != VK_NULL_HANDLE);
	vkCmdFillBuffer(cmd, buffer, 0, BUFFER_SIZE, 0xdeadbeef);
}

static VkBufferCreateInfo dummy_VkBufferCreateInfo()
{
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = BUFFER_SIZE;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vulkan.garbage_pointers)
	{
		bufferCreateInfo.queueFamilyIndexCount = 1000000; // make sure we crash if we use this
		bufferCreateInfo.pQueueFamilyIndices = (const uint32_t*)0xdeadbeef;
	}
	return bufferCreateInfo;
}

static VkBuffer dummy_vkCreateBuffer(VkDeviceSize offset)
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkBufferCreateInfo bufferCreateInfo = dummy_VkBufferCreateInfo();
	VkResult result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer);
	assert(result == VK_SUCCESS);
	assert(buffer != VK_NULL_HANDLE);
	result = vkBindBufferMemory(vulkan.device, buffer, memory, offset);
	assert(result == VK_SUCCESS);
	return buffer;
}

static void reset_buffers()
{
	for (int i = 0; i < MAX_BUFFERS; i++)
	{
		vkDestroyBuffer(vulkan.device, buffers[i].load(), nullptr);
		buffers[i] = VK_NULL_HANDLE;
	}
	next_buffer = 0;
}

static void thread_buffers()
{
	assert(next_buffer == 0);
	for (int i = 0; i < MAX_BUFFERS; i++)
	{
		buffers[i] = dummy_vkCreateBuffer(aligned_buffer_size * i);
		next_buffer++;
		if (sleep_time) usleep(sleep_time);
	}
}

static void thread_buffers_fast()
{
	assert(next_buffer == 0);
	for (int i = 0; i < MAX_BUFFERS; i++)
	{
		buffers[i] = dummy_vkCreateBuffer(aligned_buffer_size * i);
		next_buffer++;
	}
}

static void show_usage()
{
	printf("-S/--sleep-time        Thread synchronization sleep time\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-S", "--sleep-time"))
	{
		sleep_time = get_arg(argv, ++i, argc);
		return true;
	}
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;
	vulkan = test_init(argc, argv, "vulkan_thread_4", reqs);
	std::thread* helper = nullptr;
	VkResult result;

	for (int i = 0; i < MAX_BUFFERS; i++) { buffers[i] = VK_NULL_HANDLE; offsets[i] = 0; }

	VkMemoryRequirements memory_requirements;
	if (reqs.apiVersion >= VK_API_VERSION_1_3)
	{
		VkDeviceBufferMemoryRequirements reqinfo = { VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS, nullptr };
		VkBufferCreateInfo bci = dummy_VkBufferCreateInfo();
		reqinfo.pCreateInfo = &bci;
		VkMemoryRequirements2 memreqs = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
		vkGetDeviceBufferMemoryRequirements(vulkan.device, &reqinfo, &memreqs);
		memory_requirements = memreqs.memoryRequirements;
	}
	else
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkBufferCreateInfo bufferCreateInfo = dummy_VkBufferCreateInfo();
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer);
		assert(result == VK_SUCCESS);
		assert(buffer != VK_NULL_HANDLE);
		vkGetBufferMemoryRequirements(vulkan.device, buffer, &memory_requirements);
		vkDestroyBuffer(vulkan.device, buffer, nullptr);
	}
	const VkMemoryPropertyFlagBits memoryflags = (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, memoryflags);
	const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
	aligned_buffer_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);
	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_buffer_size * MAX_BUFFERS;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	assert(result == VK_SUCCESS);
	assert(memory != VK_NULL_HANDLE);

	VkCommandPoolCreateInfo cmdcreateinfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	cmdcreateinfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdcreateinfo.queueFamilyIndex = 0;
	result = vkCreateCommandPool(vulkan.device, &cmdcreateinfo, nullptr, &pool);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)pool, "Pool");

	VkCommandBufferAllocateInfo pAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	pAllocateInfo.commandBufferCount = 1;
	pAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	pAllocateInfo.commandPool = pool;
	pAllocateInfo.pNext = nullptr;
	result = vkAllocateCommandBuffers(vulkan.device, &pAllocateInfo, &cmd);
	check(result);

	VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	// Serialized
	result = vkBeginCommandBuffer(cmd, &command_buffer_begin_info);
	check(result);
	for (int i = 0; i < MAX_BUFFERS; i++)
	{
		VkBuffer buffer = dummy_vkCreateBuffer(i * aligned_buffer_size);
		dummy_vkCmdBindIndexBuffer(cmd, buffer);
		buffers[i] = buffer;
	}
	result = vkEndCommandBuffer(cmd);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool, 0);
	check(result);
	reset_buffers();

	// Threaded
	assert(next_buffer == 0);
	result = vkBeginCommandBuffer(cmd, &command_buffer_begin_info);
	check(result);
	helper = new std::thread(thread_buffers);
	for (int i = 0; i < MAX_BUFFERS; i++)
	{
		while (next_buffer.load(std::memory_order_seq_cst) <= i) usleep(sleep_time); // spin until we got it
		VkBuffer buffer = buffers[i].load(std::memory_order_seq_cst);
		assert(buffer != VK_NULL_HANDLE);
		dummy_vkCmdBindIndexBuffer(cmd, buffer); // we got it, use it
	}
	helper->join();
	delete helper;
	helper = nullptr;
	result = vkEndCommandBuffer(cmd);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool, 0);
	check(result);
	reset_buffers();

	// Threaded #2
	assert(next_buffer == 0);
	result = vkBeginCommandBuffer(cmd, &command_buffer_begin_info);
	check(result);
	helper = new std::thread(thread_buffers);
	while (next_buffer.load(std::memory_order_seq_cst) < MAX_BUFFERS) usleep(sleep_time); // spin until we got all
	for (int i = 0; i < MAX_BUFFERS; i++)
	{
		VkBuffer buffer = buffers[i].load(std::memory_order_seq_cst);
		assert(buffer != VK_NULL_HANDLE);
		dummy_vkCmdFillBuffer(cmd, buffer);
	}
	helper->join();
	delete helper;
	helper = nullptr;
	result = vkEndCommandBuffer(cmd);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool, 0);
	check(result);
	reset_buffers();

	// Threaded #3
	assert(next_buffer == 0);
	result = vkBeginCommandBuffer(cmd, &command_buffer_begin_info);
	check(result);
	helper = new std::thread(thread_buffers_fast);
	int i = 0;
	while (i < MAX_BUFFERS)
	{
		while (next_buffer.load(std::memory_order_seq_cst) <= i) usleep(sleep_time); // spin until we got some
		const int lim = next_buffer.load(std::memory_order_seq_cst);
		int count = lim - i;
		if (count > (int)vulkan.device_properties.limits.maxVertexInputBindings) count = vulkan.device_properties.limits.maxVertexInputBindings;
		std::vector<VkBuffer> avail(count);
		for (int j = 0; j < count; j++)
		{
			VkBuffer buffer = buffers[i + j].load(std::memory_order_seq_cst);
			assert(buffer != VK_NULL_HANDLE);
			avail[j] = buffer;
		}
		dummy_vkCmdBindVertexBuffers(cmd, avail);
		i += count;
	}
	helper->join();
	delete helper;
	helper = nullptr;
	result = vkEndCommandBuffer(cmd);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool, 0);
	check(result);
	reset_buffers();

	vkFreeCommandBuffers(vulkan.device, pool, 1, &cmd);
	vkDestroyCommandPool(vulkan.device, pool, nullptr);
	vkFreeMemory(vulkan.device, memory, nullptr);
	test_done(vulkan);
	return 0;
}
