// Minimal command buffer test using VK_EXT_descriptor_heap.

#include "vulkan_common.h"

#include <cstring>

struct buffer_allocation
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceAddress device_address = 0;
	void* mapped = nullptr;
	VkDeviceSize buffer_size = 0;
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

static VkDeviceSize max_size(VkDeviceSize a, VkDeviceSize b)
{
	return (a > b) ? a : b;
}

static VkDeviceSize align_up_size(VkDeviceSize value, VkDeviceSize alignment)
{
	if (alignment == 0) return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static VkDeviceAddress align_up_address(VkDeviceAddress value, VkDeviceSize alignment)
{
	if (alignment == 0) return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static buffer_allocation create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage, const char* name)
{
	buffer_allocation out{};
	out.buffer_size = size;

	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &out.buffer);
	check(result);

	VkMemoryRequirements memreq{};
	vkGetBufferMemoryRequirements(vulkan.device, out.buffer, &memreq);

	VkMemoryAllocateFlagsInfo flags_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr };
	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	alloc_info.allocationSize = memreq.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0)
	{
		flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		alloc_info.pNext = &flags_info;
	}
	result = vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &out.memory);
	check(result);
	result = vkBindBufferMemory(vulkan.device, out.buffer, out.memory, 0);
	check(result);
	result = vkMapMemory(vulkan.device, out.memory, 0, memreq.size, 0, &out.mapped);
	check(result);
	assert(out.mapped != nullptr);
	std::memset(out.mapped, 0, static_cast<size_t>(memreq.size));

	if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0)
	{
		VkBufferDeviceAddressInfo address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
		address_info.buffer = out.buffer;
		out.device_address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
		assert(out.device_address != 0);
	}

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)out.buffer, name);
	return out;
}

static void destroy_buffer(const vulkan_setup_t& vulkan, buffer_allocation& buffer)
{
	if (buffer.mapped != nullptr)
	{
		vkUnmapMemory(vulkan.device, buffer.memory);
		buffer.mapped = nullptr;
	}
	if (buffer.buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan.device, buffer.buffer, nullptr);
		buffer.buffer = VK_NULL_HANDLE;
	}
	if (buffer.memory != VK_NULL_HANDLE)
	{
		testFreeMemory(vulkan, buffer.memory);
		buffer.memory = VK_NULL_HANDLE;
	}
}

int main(int argc, char** argv)
{
	VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, nullptr };
	maintenance5_features.maintenance5 = VK_TRUE;
	VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT, &maintenance5_features };
	descriptor_heap_features.descriptorHeap = VK_TRUE;

	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.bufferDeviceAddress = true;
	reqs.device_extensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&descriptor_heap_features);
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vk = test_init(argc, argv, "vulkan_descriptor_heap_1", reqs);
	VkResult result;

	MAKEDEVICEPROCADDR(vk, vkWriteSamplerDescriptorsEXT);
	MAKEDEVICEPROCADDR(vk, vkWriteResourceDescriptorsEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdBindSamplerHeapEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdBindResourceHeapEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdPushDataEXT);

	VkPhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &descriptor_heap_properties };
	vkGetPhysicalDeviceProperties2(vk.physical, &props2);

	const VkDeviceSize sampler_descriptor_size = descriptor_heap_properties.samplerDescriptorSize;
	const VkDeviceSize resource_descriptor_size = descriptor_heap_properties.bufferDescriptorSize;
	assert(sampler_descriptor_size > 0);
	assert(resource_descriptor_size > 0);

	const VkDeviceSize sampler_reserved_offset = align_up_size(
		sampler_descriptor_size,
		max_size(descriptor_heap_properties.samplerDescriptorAlignment, descriptor_heap_properties.samplerHeapAlignment));
	const VkDeviceSize resource_reserved_offset = align_up_size(
		resource_descriptor_size,
		max_size(descriptor_heap_properties.bufferDescriptorAlignment, descriptor_heap_properties.resourceHeapAlignment));
	const VkDeviceSize sampler_heap_size = sampler_reserved_offset + descriptor_heap_properties.minSamplerHeapReservedRange;
	const VkDeviceSize resource_heap_size = resource_reserved_offset + descriptor_heap_properties.minResourceHeapReservedRange;
	assert(sampler_heap_size <= descriptor_heap_properties.maxSamplerHeapSize);
	assert(resource_heap_size <= descriptor_heap_properties.maxResourceHeapSize);

	const VkDeviceSize sampler_buffer_size = sampler_heap_size + max_size(descriptor_heap_properties.samplerHeapAlignment, 1) - 1;
	const VkDeviceSize resource_heap_buffer_size = resource_heap_size + max_size(descriptor_heap_properties.resourceHeapAlignment, 1) - 1;
	buffer_allocation sampler_heap = create_buffer(
		vk,
		sampler_buffer_size,
		VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		"descriptor_heap_sampler_heap");
	buffer_allocation resource_heap = create_buffer(
		vk,
		resource_heap_buffer_size,
		VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		"descriptor_heap_resource_heap");
	buffer_allocation resource_buffer = create_buffer(
		vk,
		256,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		"descriptor_heap_storage_buffer");

	const VkDeviceAddress sampler_heap_base = align_up_address(sampler_heap.device_address, descriptor_heap_properties.samplerHeapAlignment);
	const VkDeviceAddress resource_heap_base = align_up_address(resource_heap.device_address, descriptor_heap_properties.resourceHeapAlignment);
	const VkDeviceSize sampler_heap_offset = sampler_heap_base - sampler_heap.device_address;
	const VkDeviceSize resource_heap_offset = resource_heap_base - resource_heap.device_address;
	assert(sampler_heap_offset + sampler_heap_size <= sampler_heap.buffer_size);
	assert(resource_heap_offset + resource_heap_size <= resource_heap.buffer_size);

	VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr };
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.maxLod = 1.0f;

	VkHostAddressRangeEXT sampler_descriptor_range{};
	sampler_descriptor_range.address = static_cast<uint8_t*>(sampler_heap.mapped) + sampler_heap_offset;
	sampler_descriptor_range.size = static_cast<size_t>(sampler_descriptor_size);
	result = pf_vkWriteSamplerDescriptorsEXT(vk.device, 1, &sampler_info, &sampler_descriptor_range);
	check(result);

	VkDeviceAddressRangeEXT resource_address_range{};
	resource_address_range.address = resource_buffer.device_address;
	resource_address_range.size = resource_buffer.buffer_size;
	VkResourceDescriptorInfoEXT resource_info = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT, nullptr };
	resource_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	resource_info.data.pAddressRange = &resource_address_range;
	VkHostAddressRangeEXT resource_descriptor_range{};
	resource_descriptor_range.address = static_cast<uint8_t*>(resource_heap.mapped) + resource_heap_offset;
	resource_descriptor_range.size = static_cast<size_t>(resource_descriptor_size);
	result = pf_vkWriteResourceDescriptorsEXT(vk.device, 1, &resource_info, &resource_descriptor_range);
	check(result);

	testFlushMemoryDescriptors(vk, sampler_heap.memory, 0, sampler_heap.buffer_size, { sampler_heap_offset }, { VK_DESCRIPTOR_TYPE_SAMPLER });
	testFlushMemoryDescriptors(vk, resource_heap.memory, 0, resource_heap.buffer_size, { resource_heap_offset }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER });

	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = 0;
	result = vkCreateCommandPool(vk.device, &pool_info, nullptr, &command_pool);
	check(result);
	test_set_name(vk, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)command_pool, "descriptor_heap_command_pool");

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	alloc_info.commandPool = command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vk.device, &alloc_info, &command_buffer);
	check(result);
	test_set_name(vk, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer, "descriptor_heap_command_buffer");

	test_marker_mention(vk, "Descriptor heap buffers are ready", VK_OBJECT_TYPE_BUFFER, (uint64_t)resource_heap.buffer);
	test_marker_mention(vk, "Sampler heap buffer is ready", VK_OBJECT_TYPE_BUFFER, (uint64_t)sampler_heap.buffer);

	bench_start_iteration(vk.bench);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkBindHeapInfoEXT sampler_bind_info = { VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT, nullptr };
	sampler_bind_info.heapRange.address = sampler_heap_base;
	sampler_bind_info.heapRange.size = sampler_heap_size;
	sampler_bind_info.reservedRangeOffset = sampler_reserved_offset;
	sampler_bind_info.reservedRangeSize = descriptor_heap_properties.minSamplerHeapReservedRange;
	pf_vkCmdBindSamplerHeapEXT(command_buffer, &sampler_bind_info);

	VkBindHeapInfoEXT resource_bind_info = { VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT, nullptr };
	resource_bind_info.heapRange.address = resource_heap_base;
	resource_bind_info.heapRange.size = resource_heap_size;
	resource_bind_info.reservedRangeOffset = resource_reserved_offset;
	resource_bind_info.reservedRangeSize = descriptor_heap_properties.minResourceHeapReservedRange;
	pf_vkCmdBindResourceHeapEXT(command_buffer, &resource_bind_info);

	const std::array<uint32_t, 4> push_words = { 0x12345678u, 0xabcdef01u, 0x0badc0deu, 0xfeedfaceu };
	assert(sizeof(push_words) <= descriptor_heap_properties.maxPushDataSize);
	VkPushDataInfoEXT push_info = { VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT, nullptr };
	push_info.offset = 0;
	push_info.data.address = push_words.data();
	push_info.data.size = sizeof(push_words);
	pf_vkCmdPushDataEXT(command_buffer, &push_info);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vk.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	result = vkCreateFence(vk.device, &fence_info, nullptr, &fence);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
	result = vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	bench_stop_iteration(vk.bench);

	vkDestroyFence(vk.device, fence, nullptr);
	vkFreeCommandBuffers(vk.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vk.device, command_pool, nullptr);

	destroy_buffer(vk, resource_buffer);
	destroy_buffer(vk, resource_heap);
	destroy_buffer(vk, sampler_heap);

	test_done(vk);
	return 0;
}
