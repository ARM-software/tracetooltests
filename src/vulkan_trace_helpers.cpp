#include "vulkan_common.h"

#include <array>
#include <cstring>
#include <inttypes.h>
#include <vector>

namespace
{
constexpr VkDeviceSize kPayloadSize = 6 * sizeof(uint64_t);
constexpr uint32_t kMarkCount = 5;

struct TestBuffer
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceAddress address = 0;
};

VkMarkedOffsetsARM make_markings(const std::array<VkDeviceSize, kMarkCount>& offsets,
                                 const std::array<VkMarkingTypeARM, kMarkCount>& marking_types,
                                 const std::array<VkMarkingSubTypeARM, kMarkCount>& sub_types)
{
	VkMarkedOffsetsARM markings{VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr};
	markings.count = kMarkCount;
	markings.pMarkingTypes = marking_types.data();
	markings.pSubTypes = sub_types.data();
	markings.pOffsets = offsets.data();
	return markings;
}

TestBuffer create_test_buffer(const vulkan_setup_t& vulkan, const char* name)
{
	TestBuffer result;

	VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	buffer_info.size = kPayloadSize;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult vk_result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &result.buffer);
	check(vk_result);

	std::vector<VkBuffer> buffers{result.buffer};
	std::vector<VkDeviceMemory> memories;
	const uint32_t aligned_size = testAllocateBufferMemory(vulkan, buffers, memories, true, true, false, name);
	assert(aligned_size >= kPayloadSize);
	assert(memories.size() == 1);
	result.memory = memories[0];

	VkBufferDeviceAddressInfo address_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr};
	address_info.buffer = result.buffer;
	result.address = vkGetBufferDeviceAddress(vulkan.device, &address_info);
	assert(result.address != 0);

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)result.buffer, name);
	test_marker_mention(vulkan, std::string(name) + " ready for trace helper update", VK_OBJECT_TYPE_BUFFER, (uint64_t)result.buffer);

	return result;
}

void verify_memory(const vulkan_setup_t& vulkan, VkDeviceMemory memory, const std::array<uint64_t, 6>& expected)
{
	void* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, memory, 0, kPayloadSize, 0, &mapped);
	check(result);
	assert(std::memcmp(mapped, expected.data(), kPayloadSize) == 0);
	vkUnmapMemory(vulkan.device, memory);
}
}

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.required_queue_flags = VK_QUEUE_TRANSFER_BIT;
	reqs.bufferDeviceAddress = true;
	reqs.device_extensions.push_back("VK_ARM_trace_helpers");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_trace_helpers", reqs);

	assert(vulkan.has_trace_helpers);
	assert(vulkan.vkCmdUpdateBuffer2);
	assert(vulkan.vkCmdUpdateMemory2);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	TestBuffer update_buffer = create_test_buffer(vulkan, "trace-helper-update-buffer");
	TestBuffer update_memory = create_test_buffer(vulkan, "trace-helper-update-memory");

	const std::array<VkDeviceSize, kMarkCount> marked_offsets = {
		0,
		sizeof(uint64_t),
		2 * sizeof(uint64_t),
		3 * sizeof(uint64_t),
		4 * sizeof(uint64_t),
	};
	const std::array<VkMarkingTypeARM, kMarkCount> marking_types = {
		VK_MARKING_TYPE_DEVICE_ADDRESS_ARM,
		VK_MARKING_TYPE_DESCRIPTOR_SIZE_ARM,
		VK_MARKING_TYPE_DESCRIPTOR_OFFSET_ARM,
		VK_MARKING_TYPE_DEVICE_ADDRESS_ARM,
		VK_MARKING_TYPE_DESCRIPTOR_ARM,
	};
	std::array<VkMarkingSubTypeARM, kMarkCount> sub_types{};
	sub_types[0].deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
	sub_types[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	sub_types[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	sub_types[3].deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
	sub_types[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

	VkMarkedOffsetsARM buffer_markings = make_markings(marked_offsets, marking_types, sub_types);
	VkMarkedOffsetsARM memory_markings = make_markings(marked_offsets, marking_types, sub_types);

	const std::array<uint64_t, 6> buffer_payload = {
		update_memory.address,
		32,
		16,
		update_buffer.address,
		0x1122334455667788ull,
		0x99aabbccddeeff00ull,
	};
	const std::array<uint64_t, 6> memory_payload = {
		update_buffer.address,
		64,
		24,
		update_memory.address,
		0x0102030405060708ull,
		0x8877665544332211ull,
	};

	VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkResult result = vkCreateCommandPool(vulkan.device, &command_pool_info, nullptr, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo command_buffer_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	command_buffer_info.commandPool = command_pool;
	command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_info, &command_buffer);
	check(result);

	VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr};
	VkFence fence = VK_NULL_HANDLE;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);

	bench_start_iteration(vulkan.bench);

	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkUpdateBufferInfoARM update_buffer_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, &buffer_markings};
	update_buffer_info.dstBuffer = update_buffer.buffer;
	update_buffer_info.dstOffset = 0;
	update_buffer_info.dataSize = kPayloadSize;
	update_buffer_info.pData = buffer_payload.data();
	vulkan.vkCmdUpdateBuffer2(command_buffer, &update_buffer_info);

	VkDeviceAddressRangeKHR address_range{};
	address_range.address = update_memory.address;
	address_range.size = kPayloadSize;
	VkUpdateMemoryInfoARM update_memory_info{VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_ARM, &memory_markings};
	update_memory_info.pDstRange = &address_range;
	update_memory_info.dstFlags = VK_ADDRESS_COMMAND_FULLY_BOUND_BIT_KHR | VK_ADDRESS_COMMAND_STORAGE_BUFFER_USAGE_BIT_KHR;
	update_memory_info.dataSize = kPayloadSize;
	update_memory_info.pData = memory_payload.data();
	vulkan.vkCmdUpdateMemory2(command_buffer, &update_memory_info);

	VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr};
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
	bench_stop_iteration(vulkan.bench);

	verify_memory(vulkan, update_buffer.memory, buffer_payload);
	verify_memory(vulkan, update_memory.memory, memory_payload);

	if (vulkan.vkAssertBuffer)
	{
		uint32_t checksum = 0;
		VkUpdateBufferInfoARM assert_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, &buffer_markings};
		assert_info.dstBuffer = update_buffer.buffer;
		assert_info.dstOffset = 0;
		assert_info.dataSize = kPayloadSize;
		assert_info.pData = buffer_payload.data();
		result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum, "trace helper updated buffer");
		check(result);
	}

	if (vulkan.vkAssertMemory)
	{
		uint32_t checksum = 0;
		VkUpdateMemoryInfoARM assert_info{VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_ARM, &memory_markings};
		assert_info.pDstRange = &address_range;
		assert_info.dstFlags = VK_ADDRESS_COMMAND_FULLY_BOUND_BIT_KHR | VK_ADDRESS_COMMAND_STORAGE_BUFFER_USAGE_BIT_KHR;
		assert_info.dataSize = kPayloadSize;
		assert_info.pData = memory_payload.data();
		result = vulkan.vkAssertMemory(vulkan.device, &assert_info, &checksum, "trace helper updated memory");
		check(result);
	}

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyBuffer(vulkan.device, update_buffer.buffer, nullptr);
	vkDestroyBuffer(vulkan.device, update_memory.buffer, nullptr);
	testFreeMemory(vulkan, update_buffer.memory);
	testFreeMemory(vulkan, update_memory.memory);
	test_done(vulkan);

	return 0;
}
