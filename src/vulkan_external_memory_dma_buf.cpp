#include "vulkan_common.h"

#include <unistd.h>

static constexpr VkDeviceSize DATA_SIZE = 1024;

struct buffer_resource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkMemoryRequirements requirements = {};
};

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static uint32_t find_memory_type(const vulkan_setup_t& vulkan, uint32_t type_bits, VkMemoryPropertyFlags required_flags)
{
	VkPhysicalDeviceMemoryProperties properties = {};
	vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &properties);
	for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
	{
		if ((type_bits & (1u << i)) == 0) continue;
		if ((properties.memoryTypes[i].propertyFlags & required_flags) == required_flags) return i;
	}
	return UINT32_MAX;
}

static buffer_resource create_buffer(const vulkan_setup_t& vulkan,
                                     VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     VkExternalMemoryHandleTypeFlagBits handle_type,
                                     const char* name)
{
	VkExternalMemoryBufferCreateInfo external_info = {
		VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, nullptr
	};
	external_info.handleTypes = handle_type;

	VkBufferCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	create_info.pNext = handle_type == 0 ? nullptr : &external_info;
	create_info.size = size;
	create_info.usage = usage;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	buffer_resource resource = {};
	VkResult result = vkCreateBuffer(vulkan.device, &create_info, nullptr, &resource.buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resource.buffer, name);

	vkGetBufferMemoryRequirements(vulkan.device, resource.buffer, &resource.requirements);
	return resource;
}

static void bind_memory(const vulkan_setup_t& vulkan, buffer_resource& resource)
{
	VkResult result = vkBindBufferMemory(vulkan.device, resource.buffer, resource.memory, 0);
	check(result);
}

static void allocate_staging_memory(const vulkan_setup_t& vulkan, buffer_resource& resource)
{
	uint32_t memory_type_index = find_memory_type(vulkan,
	                                             resource.requirements.memoryTypeBits,
	                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	bool coherent = true;
	if (memory_type_index == UINT32_MAX)
	{
		memory_type_index = find_memory_type(vulkan,
		                                     resource.requirements.memoryTypeBits,
		                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		coherent = false;
	}
	if (memory_type_index == UINT32_MAX)
	{
		printf("Skipping: no host-visible memory type for staging buffer\n");
		exit(77);
	}

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = resource.requirements.size;
	allocate_info.memoryTypeIndex = memory_type_index;

	VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)resource.memory, "external-memory-fd-staging-memory");
	bind_memory(vulkan, resource);

	std::vector<uint8_t> pattern(DATA_SIZE);
	for (uint32_t i = 0; i < pattern.size(); i++) pattern[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xffu);

	void* data = nullptr;
	result = vkMapMemory(vulkan.device, resource.memory, 0, resource.requirements.size, 0, &data);
	check(result);
	memcpy(data, pattern.data(), pattern.size());
	if (!coherent || vulkan.has_explicit_host_updates) testFlushMemory(vulkan, resource.memory, 0, VK_WHOLE_SIZE, coherent);
	vkUnmapMemory(vulkan.device, resource.memory);
}

static void allocate_export_memory(const vulkan_setup_t& vulkan,
                                   buffer_resource& resource,
                                   VkExternalMemoryHandleTypeFlagBits handle_type,
                                   bool dedicated)
{
	uint32_t memory_type_index = find_memory_type(vulkan, resource.requirements.memoryTypeBits, 0);
	if (memory_type_index == UINT32_MAX)
	{
		printf("Skipping: no memory type for exportable external buffer\n");
		exit(77);
	}

	VkMemoryDedicatedAllocateInfo dedicated_info = {
		VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr
	};
	dedicated_info.image = VK_NULL_HANDLE;
	dedicated_info.buffer = resource.buffer;

	VkExportMemoryAllocateInfo export_info = {
		VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, dedicated ? &dedicated_info : nullptr
	};
	export_info.handleTypes = handle_type;

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &export_info };
	allocate_info.allocationSize = resource.requirements.size;
	allocate_info.memoryTypeIndex = memory_type_index;

	VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)resource.memory, "external-memory-fd-exported-memory");
	bind_memory(vulkan, resource);
}

static void allocate_import_memory(const vulkan_setup_t& vulkan,
                                   buffer_resource& resource,
                                   VkExternalMemoryHandleTypeFlagBits handle_type,
                                   int fd,
                                   uint32_t import_memory_type_bits,
                                   bool dedicated)
{
	const uint32_t compatible_type_bits = resource.requirements.memoryTypeBits & import_memory_type_bits;
	uint32_t memory_type_index = find_memory_type(vulkan, compatible_type_bits, 0);
	if (memory_type_index == UINT32_MAX)
	{
		close(fd);
		printf("Skipping: no compatible memory type for imported fd\n");
		exit(77);
	}

	VkMemoryDedicatedAllocateInfo dedicated_info = {
		VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr
	};
	dedicated_info.image = VK_NULL_HANDLE;
	dedicated_info.buffer = resource.buffer;

	VkImportMemoryFdInfoKHR import_info = {
		VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, dedicated ? &dedicated_info : nullptr
	};
	import_info.handleType = handle_type;
	import_info.fd = fd;

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import_info };
	allocate_info.allocationSize = resource.requirements.size;
	allocate_info.memoryTypeIndex = memory_type_index;

	VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)resource.memory, "external-memory-fd-imported-memory");
	bind_memory(vulkan, resource);
}

static void destroy_buffer_resource(const vulkan_setup_t& vulkan, buffer_resource& resource)
{
	if (resource.buffer != VK_NULL_HANDLE) vkDestroyBuffer(vulkan.device, resource.buffer, nullptr);
	if (resource.memory != VK_NULL_HANDLE) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_external_memory_fd", reqs);

	const VkExternalMemoryHandleTypeFlagBits handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	VkPhysicalDeviceExternalBufferInfo external_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO, nullptr
	};
	external_info.flags = 0;
	external_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	external_info.handleType = handle_type;

	VkExternalBufferProperties external_properties = {
		VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES, nullptr
	};
	vkGetPhysicalDeviceExternalBufferProperties(vulkan.physical, &external_info, &external_properties);

	const VkExternalMemoryFeatureFlags required_features =
		VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
		VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
	const VkExternalMemoryProperties& memory_properties = external_properties.externalMemoryProperties;
	if ((memory_properties.externalMemoryFeatures & required_features) != required_features)
	{
		printf("Skipping: DMA_BUF external memory import/export not supported for transfer buffers\n");
		test_done(vulkan);
		return 77;
	}
	if ((memory_properties.compatibleHandleTypes & handle_type) == 0)
	{
		printf("Skipping: DMA_BUF is not a compatible external memory handle type\n");
		test_done(vulkan);
		return 77;
	}
	const bool dedicated = (memory_properties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0;

	MAKEDEVICEPROCADDR(vulkan, vkGetMemoryFdKHR);
	MAKEDEVICEPROCADDR(vulkan, vkGetMemoryFdPropertiesKHR);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	bench_start_iteration(vulkan.bench);

	buffer_resource staging = create_buffer(vulkan,
	                                        DATA_SIZE,
	                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                                        static_cast<VkExternalMemoryHandleTypeFlagBits>(0),
	                                        "external-memory-fd-staging-buffer");
	allocate_staging_memory(vulkan, staging);

	buffer_resource exported = create_buffer(vulkan,
	                                         DATA_SIZE,
	                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                         handle_type,
	                                         "external-memory-fd-exported-buffer");
	allocate_export_memory(vulkan, exported, handle_type, dedicated);

	VkMemoryGetFdInfoKHR get_fd_info = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, nullptr };
	get_fd_info.memory = exported.memory;
	get_fd_info.handleType = handle_type;
	int exported_fd = -1;
	VkResult result = pf_vkGetMemoryFdKHR(vulkan.device, &get_fd_info, &exported_fd);
	check(result);
	assert(exported_fd >= 0);

	VkMemoryFdPropertiesKHR fd_properties = {
		VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR, nullptr
	};
	result = pf_vkGetMemoryFdPropertiesKHR(vulkan.device, handle_type, exported_fd, &fd_properties);
	check(result);
	assert(fd_properties.memoryTypeBits != 0);

	buffer_resource imported = create_buffer(vulkan,
	                                         DATA_SIZE,
	                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                         handle_type,
	                                         "external-memory-fd-imported-buffer");
	allocate_import_memory(vulkan, imported, handle_type, exported_fd, fd_properties.memoryTypeBits, dedicated);
	exported_fd = -1;

	testCopyBuffer(vulkan, queue, exported.buffer, staging.buffer, DATA_SIZE);
	test_marker_mention(vulkan, "Submitted external memory fd buffers", VK_OBJECT_TYPE_BUFFER, (uint64_t)imported.buffer);
	testQueueBuffer(vulkan, queue, { exported.buffer, imported.buffer });

	if (vulkan.vkAssertBuffer)
	{
		uint32_t exported_crc = 0;
		uint32_t imported_crc = 0;
		const VkUpdateBufferInfoARM exported_info = {
			VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, exported.buffer, 0, DATA_SIZE, nullptr
		};
		result = vulkan.vkAssertBuffer(vulkan.device, &exported_info, &exported_crc, "exported external memory fd buffer");
		check(result);
		const VkUpdateBufferInfoARM imported_info = {
			VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, imported.buffer, 0, DATA_SIZE, nullptr
		};
		result = vulkan.vkAssertBuffer(vulkan.device, &imported_info, &imported_crc, "imported external memory fd buffer");
		check(result);
		assert(exported_crc == imported_crc);
	}

	destroy_buffer_resource(vulkan, imported);
	destroy_buffer_resource(vulkan, exported);
	destroy_buffer_resource(vulkan, staging);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);
	return 0;
}
