#include "vulkan_common.h"

#include <array>
#include <cstring>
#include <inttypes.h>
#include <vector>

namespace
{
constexpr VkDeviceSize kPayloadDescriptorOffset = 4 * sizeof(uint64_t);
constexpr uint32_t kMarkCount = 5;

struct TestBuffer
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceAddress address = 0;
	VkDeviceSize size = 0;
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

TestBuffer create_test_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, const char* name)
{
	TestBuffer result;
	result.size = size;

	VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	buffer_info.size = size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult vk_result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &result.buffer);
	check(vk_result);

	std::vector<VkBuffer> buffers{result.buffer};
	std::vector<VkDeviceMemory> memories;
	const uint32_t aligned_size = testAllocateBufferMemory(vulkan, buffers, memories, true, true, false, name);
	assert(aligned_size >= size);
	assert(memories.size() == 1);
	result.memory = memories[0];

	VkBufferDeviceAddressInfo address_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr};
	address_info.buffer = result.buffer;
	result.address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
	assert(result.address != 0);

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)result.buffer, name);
	test_marker_mention(vulkan, std::string(name) + " ready for trace helper update", VK_OBJECT_TYPE_BUFFER, (uint64_t)result.buffer);

	return result;
}

void write_u64(std::vector<uint8_t>& payload, VkDeviceSize offset, uint64_t value)
{
	assert(offset <= payload.size());
	assert(sizeof(value) <= payload.size() - offset);
	std::memcpy(payload.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> get_storage_buffer_descriptor(const vulkan_setup_t& vulkan,
                                                   PFN_vkGetDescriptorEXT get_descriptor,
                                                   const TestBuffer& buffer,
                                                   size_t descriptor_size)
{
	assert(buffer.address != 0);
	assert(buffer.size > 0);
	assert(descriptor_size > 0);

	VkDescriptorAddressInfoEXT descriptor_address{VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT, nullptr};
	descriptor_address.address = buffer.address;
	descriptor_address.range = buffer.size;
	descriptor_address.format = VK_FORMAT_UNDEFINED;

	VkDescriptorDataEXT descriptor_data{};
	descriptor_data.pStorageBuffer = &descriptor_address;

	VkDescriptorGetInfoEXT descriptor_info{VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT, nullptr};
	descriptor_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptor_info.data = descriptor_data;

	std::vector<uint8_t> descriptor(descriptor_size);
	get_descriptor(vulkan.device, &descriptor_info, descriptor.size(), descriptor.data());
	return descriptor;
}

std::vector<uint8_t> make_payload(VkDeviceAddress first_address,
                                  VkDeviceSize descriptor_size,
                                  VkDeviceSize descriptor_offset,
                                  VkDeviceAddress second_address,
                                  const std::vector<uint8_t>& descriptor)
{
	assert(descriptor.size() == descriptor_size);
	assert(kPayloadDescriptorOffset + descriptor.size() <= UINT32_MAX);

	std::vector<uint8_t> payload(kPayloadDescriptorOffset + descriptor.size());
	write_u64(payload, 0, first_address);
	write_u64(payload, sizeof(uint64_t), descriptor_size);
	write_u64(payload, 2 * sizeof(uint64_t), descriptor_offset);
	write_u64(payload, 3 * sizeof(uint64_t), second_address);
	std::memcpy(payload.data() + kPayloadDescriptorOffset, descriptor.data(), descriptor.size());
	return payload;
}

void verify_memory(const vulkan_setup_t& vulkan, VkDeviceMemory memory, const std::vector<uint8_t>& expected)
{
	void* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, memory, 0, expected.size(), 0, &mapped);
	check(result);
	assert(std::memcmp(mapped, expected.data(), expected.size()) == 0);
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
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.required_queue_flags = VK_QUEUE_TRANSFER_BIT;
	reqs.bufferDeviceAddress = true;
	VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT, reqs.extension_features
	};
	descriptor_buffer_features.descriptorBuffer = VK_TRUE;
	VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR device_address_commands_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_ADDRESS_COMMANDS_FEATURES_KHR,
		&descriptor_buffer_features,
		VK_TRUE
	};
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&device_address_commands_features);
	reqs.device_extensions.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_DEVICE_ADDRESS_COMMANDS_EXTENSION_NAME);
	reqs.device_extensions.push_back("VK_ARM_trace_helpers");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_trace_helpers", reqs);

	if (!vulkan.has_trace_helpers)
	{
		return 77; // user probably used --no-trace-helpers
	}

	assert(vulkan.vkCmdUpdateBuffer2);
	assert(vulkan.vkCmdUpdateMemory2);
	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorSetLayoutSizeEXT);
	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorSetLayoutBindingOffsetEXT);
	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorEXT);

	VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_properties{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT,
		nullptr
	};
	VkPhysicalDeviceProperties2 device_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &descriptor_buffer_properties};
	vkGetPhysicalDeviceProperties2(vulkan.physical, &device_properties);
	assert(descriptor_buffer_properties.storageBufferDescriptorSize > 0);

	std::array<VkDescriptorSetLayoutBinding, 2> layout_bindings{};
	for (uint32_t i = 0; i < layout_bindings.size(); i++)
	{
		layout_bindings[i].binding = i;
		layout_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		layout_bindings[i].descriptorCount = 1;
		layout_bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}

	VkDescriptorSetLayoutCreateInfo descriptor_layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	descriptor_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	descriptor_layout_info.bindingCount = layout_bindings.size();
	descriptor_layout_info.pBindings = layout_bindings.data();
	VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
	VkResult result = vkCreateDescriptorSetLayout(vulkan.device, &descriptor_layout_info, nullptr, &descriptor_layout);
	check(result);

	VkDeviceSize descriptor_set_size = 0;
	pf_vkGetDescriptorSetLayoutSizeEXT(vulkan.device, descriptor_layout, &descriptor_set_size);
	assert(descriptor_set_size > 0);
	VkDeviceSize storage_buffer_descriptor_offset = 0;
	pf_vkGetDescriptorSetLayoutBindingOffsetEXT(vulkan.device, descriptor_layout, 1, &storage_buffer_descriptor_offset);
	assert(storage_buffer_descriptor_offset + descriptor_buffer_properties.storageBufferDescriptorSize <= descriptor_set_size);

	const VkDeviceSize payload_size = kPayloadDescriptorOffset + descriptor_buffer_properties.storageBufferDescriptorSize;
	assert(payload_size % 4 == 0);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	TestBuffer update_buffer = create_test_buffer(vulkan, payload_size, "trace-helper-update-buffer");
	TestBuffer update_memory = create_test_buffer(vulkan, payload_size, "trace-helper-update-memory");

	const std::array<VkDeviceSize, kMarkCount> marked_offsets = {
		0,
		sizeof(uint64_t),
		2 * sizeof(uint64_t),
		3 * sizeof(uint64_t),
		kPayloadDescriptorOffset,
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

	std::vector<uint8_t> buffer_descriptor = get_storage_buffer_descriptor(
		vulkan,
		pf_vkGetDescriptorEXT,
		update_memory,
		descriptor_buffer_properties.storageBufferDescriptorSize);
	std::vector<uint8_t> memory_descriptor = get_storage_buffer_descriptor(
		vulkan,
		pf_vkGetDescriptorEXT,
		update_buffer,
		descriptor_buffer_properties.storageBufferDescriptorSize);

	std::vector<uint8_t> buffer_payload = make_payload(
		update_memory.address,
		descriptor_buffer_properties.storageBufferDescriptorSize,
		storage_buffer_descriptor_offset,
		update_buffer.address,
		buffer_descriptor);
	std::vector<uint8_t> memory_payload = make_payload(
		update_buffer.address,
		descriptor_buffer_properties.storageBufferDescriptorSize,
		storage_buffer_descriptor_offset,
		update_memory.address,
		memory_descriptor);

	VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &command_pool_info, nullptr, &command_pool);
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
	update_buffer_info.dataSize = buffer_payload.size();
	update_buffer_info.pData = buffer_payload.data();
	vulkan.vkCmdUpdateBuffer2(command_buffer, &update_buffer_info);

	VkDeviceAddressRangeKHR address_range{};
	address_range.address = update_memory.address;
	address_range.size = memory_payload.size();
	VkUpdateMemoryInfoARM update_memory_info{VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_ARM, &memory_markings};
	update_memory_info.pDstRange = &address_range;
	update_memory_info.dstFlags = VK_ADDRESS_COMMAND_FULLY_BOUND_BIT_KHR | VK_ADDRESS_COMMAND_STORAGE_BUFFER_USAGE_BIT_KHR;
	update_memory_info.dataSize = memory_payload.size();
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
		assert_info.dataSize = buffer_payload.size();
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
		assert_info.dataSize = memory_payload.size();
		assert_info.pData = memory_payload.data();
		result = vulkan.vkAssertMemory(vulkan.device, &assert_info, &checksum, "trace helper updated memory");
		check(result);
	}

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, descriptor_layout, nullptr);
	vkDestroyBuffer(vulkan.device, update_buffer.buffer, nullptr);
	vkDestroyBuffer(vulkan.device, update_memory.buffer, nullptr);
	testFreeMemory(vulkan, update_buffer.memory);
	testFreeMemory(vulkan, update_memory.memory);
	test_done(vulkan);

	return 0;
}
