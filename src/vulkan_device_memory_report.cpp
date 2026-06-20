#include "vulkan_common.h"

#include <atomic>
#include <inttypes.h>

struct report_state
{
	std::atomic<uint32_t> total{0};
	std::atomic<uint32_t> allocate{0};
	std::atomic<uint32_t> free{0};
	std::atomic<uint32_t> import{0};
	std::atomic<uint32_t> unimport{0};
	std::atomic<uint32_t> allocation_failed{0};
};

static const char* report_event_name(VkDeviceMemoryReportEventTypeEXT type)
{
	switch (type)
	{
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT: return "allocate";
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT: return "free";
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT: return "import";
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT: return "unimport";
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATION_FAILED_EXT: return "allocation_failed";
	default: return "unknown";
	}
}

static const char* object_type_name(VkObjectType type)
{
	switch (type)
	{
	case VK_OBJECT_TYPE_INSTANCE: return "instance";
	case VK_OBJECT_TYPE_PHYSICAL_DEVICE: return "physical_device";
	case VK_OBJECT_TYPE_DEVICE: return "device";
	case VK_OBJECT_TYPE_QUEUE: return "queue";
	case VK_OBJECT_TYPE_DEVICE_MEMORY: return "device_memory";
	case VK_OBJECT_TYPE_BUFFER: return "buffer";
	case VK_OBJECT_TYPE_IMAGE: return "image";
	case VK_OBJECT_TYPE_UNKNOWN: return "unknown";
	default: return "other";
	}
}

static void VKAPI_CALL memory_report_callback(const VkDeviceMemoryReportCallbackDataEXT* data, void* user_data)
{
	assert(data);
	assert(data->sType == VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT);
	assert(user_data);

	report_state* state = reinterpret_cast<report_state*>(user_data);
	state->total.fetch_add(1);
	switch (data->type)
	{
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT:
		state->allocate.fetch_add(1);
		break;
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT:
		state->free.fetch_add(1);
		break;
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT:
		state->import.fetch_add(1);
		break;
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT:
		state->unimport.fetch_add(1);
		break;
	case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATION_FAILED_EXT:
		state->allocation_failed.fetch_add(1);
		break;
	default:
		break;
	}

	printf("memory report: event=%s memoryObjectId=%" PRIu64 " size=%" PRIu64
	       " objectType=%s(%d) objectHandle=0x%" PRIx64 " heapIndex=%u flags=0x%x\n",
	       report_event_name(data->type), data->memoryObjectId, (uint64_t)data->size,
	       object_type_name(data->objectType), (int)data->objectType, data->objectHandle,
	       data->heapIndex, data->flags);
}

static void print_memory_properties(const vulkan_setup_t& vulkan)
{
	VkPhysicalDeviceMemoryProperties memory_properties = {};
	vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &memory_properties);

	printf("memory heaps: %u\n", memory_properties.memoryHeapCount);
	for (uint32_t i = 0; i < memory_properties.memoryHeapCount; i++)
	{
		printf("\theap %u: size=%" PRIu64 " flags=0x%x\n",
		       i, (uint64_t)memory_properties.memoryHeaps[i].size,
		       memory_properties.memoryHeaps[i].flags);
	}

	printf("memory types: %u\n", memory_properties.memoryTypeCount);
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
	{
		printf("\ttype %u: heap=%u flags=0x%x\n",
		       i, memory_properties.memoryTypes[i].heapIndex,
		       memory_properties.memoryTypes[i].propertyFlags);
	}
}

static void print_report_summary(const report_state& state, const char* label)
{
	printf("%s: total=%u allocate=%u free=%u import=%u unimport=%u allocation_failed=%u\n",
	       label,
	       state.total.load(),
	       state.allocate.load(),
	       state.free.load(),
	       state.import.load(),
	       state.unimport.load(),
	       state.allocation_failed.load());
}

int main(int argc, char** argv)
{
	report_state state;
	VkPhysicalDeviceDeviceMemoryReportFeaturesEXT report_features =
		{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT, nullptr, VK_TRUE };
	VkDeviceDeviceMemoryReportCreateInfoEXT report_create_info =
		{ VK_STRUCTURE_TYPE_DEVICE_DEVICE_MEMORY_REPORT_CREATE_INFO_EXT, &report_features, 0, memory_report_callback, &state };

	vulkan_req_t reqs{};
	reqs.device_extensions.push_back(VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&report_create_info);
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_device_memory_report", reqs);

	print_memory_properties(vulkan);

	bench_start_iteration(vulkan.bench);

	const VkDeviceSize buffer_size = 64 * 1024;
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

	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	vkFreeMemory(vulkan.device, memory, nullptr);
	assert(state.allocate.load() > 0);
	assert(state.free.load() > 0);

	bench_stop_iteration(vulkan.bench);

	print_report_summary(state, "reports before device destroy");
	test_done(vulkan);
	print_report_summary(state, "reports after device destroy");

	return 0;
}
