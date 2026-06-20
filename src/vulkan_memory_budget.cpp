#include "vulkan_common.h"

#include <inttypes.h>

static VkPhysicalDeviceMemoryProperties2 get_memory_budget_properties(const vulkan_setup_t& vulkan, VkPhysicalDeviceMemoryBudgetPropertiesEXT& budget)
{
	budget = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceMemoryProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &budget };
	vkGetPhysicalDeviceMemoryProperties2(vulkan.physical, &properties);
	assert(properties.memoryProperties.memoryHeapCount > 0);
	return properties;
}

static void print_memory_budget(const vulkan_setup_t& vulkan, const char* label)
{
	VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {};
	VkPhysicalDeviceMemoryProperties2 properties = get_memory_budget_properties(vulkan, budget);

	printf("%s:\n", label);
	printf("\tmemory heaps: %u\n", properties.memoryProperties.memoryHeapCount);
	for (uint32_t i = 0; i < properties.memoryProperties.memoryHeapCount; i++)
	{
		const VkMemoryHeap& heap = properties.memoryProperties.memoryHeaps[i];
		printf("\theap %u: size=%" PRIu64 " budget=%" PRIu64 " usage=%" PRIu64 " flags=0x%x\n",
		       i, (uint64_t)heap.size, (uint64_t)budget.heapBudget[i],
		       (uint64_t)budget.heapUsage[i], heap.flags);
	}

	printf("\tmemory types: %u\n", properties.memoryProperties.memoryTypeCount);
	for (uint32_t i = 0; i < properties.memoryProperties.memoryTypeCount; i++)
	{
		const VkMemoryType& type = properties.memoryProperties.memoryTypes[i];
		printf("\ttype %u: heap=%u flags=0x%x\n", i, type.heapIndex, type.propertyFlags);
	}
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.device_extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_memory_budget", reqs);

	bench_start_iteration(vulkan.bench);

	print_memory_budget(vulkan, "memory budget before allocation");

	const VkDeviceSize buffer_size = 64 * 1024 * 1024;
	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = buffer_size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer buffer = VK_NULL_HANDLE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &buffer);
	check(result);
	assert(buffer != VK_NULL_HANDLE);

	VkMemoryRequirements memory_requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, buffer, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = memory_requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, 0);

	printf("allocating buffer memory: bufferSize=%" PRIu64 " allocationSize=%" PRIu64
	       " memoryTypeIndex=%u memoryTypeBits=0x%x\n",
	       (uint64_t)buffer_size, (uint64_t)allocate_info.allocationSize,
	       allocate_info.memoryTypeIndex, memory_requirements.memoryTypeBits);

	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &memory);
	check(result);
	assert(memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, buffer, memory, 0);
	check(result);

	print_memory_budget(vulkan, "memory budget after allocation");

	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	vkFreeMemory(vulkan.device, memory, nullptr);

	print_memory_budget(vulkan, "memory budget after free");

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);
	return 0;
}
