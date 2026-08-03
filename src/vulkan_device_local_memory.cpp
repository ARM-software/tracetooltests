#include "vulkan_common.h"

static bool find_device_local_only_memory_type(const vulkan_setup_t &vulkan, uint32_t type_filter, uint32_t *memory_type_index)
{
	VkPhysicalDeviceMemoryProperties props = {};
	vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &props);

	for (uint32_t i = 0; i < props.memoryTypeCount; i++)
	{
		const VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
		const bool supported = (type_filter & (1u << i)) != 0;
		const bool device_local = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
		const bool host_visible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

		if (supported && device_local && !host_visible)
		{
			*memory_type_index = i;
			return true;
		}
	}

	return false;
}

static void destroy_buffer(const vulkan_setup_t &vulkan, VkBuffer *buffer, VkDeviceMemory *memory)
{
	if (*buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan.device, *buffer, nullptr);
		*buffer = VK_NULL_HANDLE;
	}

	if (*memory != VK_NULL_HANDLE)
	{
		testFreeMemory(vulkan, *memory);
		*memory = VK_NULL_HANDLE;
	}
}

static int create_buffer(const vulkan_setup_t &vulkan, VkBufferUsageFlags usage, bool device_local_only, VkBuffer *buffer, VkDeviceMemory *memory)
{
	VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.size = 512;
	bufferCreateInfo.usage = usage;

	VkResult result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, buffer);
	check(result);

	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(vulkan.device, *buffer, &requirements);

	VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	alloc.allocationSize = requirements.size;

	if (device_local_only)
	{
		if (!find_device_local_only_memory_type(vulkan, requirements.memoryTypeBits, &alloc.memoryTypeIndex))
		{
			printf("Skipping: no DEVICE_LOCAL memory type without HOST_VISIBLE for this buffer\n");
			vkDestroyBuffer(vulkan.device, *buffer, nullptr);
			*buffer = VK_NULL_HANDLE;
			return 77;
		}
	}
	else
	{
		alloc.memoryTypeIndex = get_device_memory_type(
			requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}
	result = vkAllocateMemory(vulkan.device, &alloc, nullptr, memory);
	check(result);
	result = vkBindBufferMemory(vulkan.device, *buffer, *memory, 0);
	check(result);
	return 0;
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_device_local_memory", reqs);

	VkBuffer device_buffer = VK_NULL_HANDLE;
	VkBuffer upload_buffer = VK_NULL_HANDLE;
	VkBuffer readback_buffer = VK_NULL_HANDLE;
	VkDeviceMemory device_memory = VK_NULL_HANDLE;
	VkDeviceMemory upload_memory = VK_NULL_HANDLE;
	VkDeviceMemory readback_memory = VK_NULL_HANDLE;
	uint32_t expected_crc = 0;
	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	int ret = create_buffer(
		vulkan,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		true,
		&device_buffer,
		&device_memory);

	if (ret == 0)
		ret = create_buffer(vulkan, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, &upload_buffer, &upload_memory);

	if (ret == 0)
		ret = create_buffer(vulkan, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, &readback_buffer, &readback_memory);

	if (ret != 0)
	{
		destroy_buffer(vulkan, &readback_buffer, &readback_memory);
		destroy_buffer(vulkan, &upload_buffer, &upload_memory);
		destroy_buffer(vulkan, &device_buffer, &device_memory);
		test_done(vulkan);
		return ret;
	}

	bench_start_iteration(vulkan.bench);
	char *data = nullptr;
	VkResult result = vkMapMemory(vulkan.device, upload_memory, 0, 512, 0, (void **)&data);
	check(result);
	memset(data, 0xdeaddead, 512);
	expected_crc = adler32((unsigned char *)data, 512);
	if (vulkan.has_explicit_host_updates)
		testFlushMemory(vulkan, upload_memory, 0, 512, true);

	vkUnmapMemory(vulkan.device, upload_memory);

	testCopyBuffer(vulkan, queue, device_buffer, upload_buffer, 512);
	testCopyBuffer(vulkan, queue, readback_buffer, device_buffer, 512);

	result = vkMapMemory(vulkan.device, readback_memory, 0, 512, 0, (void **)&data);
	check(result);

	uint32_t actual_crc = adler32((unsigned char *)data, 512);
	vkUnmapMemory(vulkan.device, readback_memory);

	if (vulkan.vkAssertBuffer)
	{
		uint32_t assert_crc = 0;
		const VkUpdateBufferInfoARM buffer_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, readback_buffer, 0, VK_WHOLE_SIZE, nullptr};
		result = vulkan.vkAssertBuffer(vulkan.device, &buffer_info, &assert_crc, "readback buffer for crc");
		check(result);
		if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
		{
			assert(expected_crc == assert_crc);
			assert(actual_crc == assert_crc);
		}
		(void)expected_crc;
		(void)actual_crc;
	}
	else if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		assert(actual_crc == expected_crc);
	}
	bench_stop_iteration(vulkan.bench);
	destroy_buffer(vulkan, &readback_buffer, &readback_memory);
	destroy_buffer(vulkan, &upload_buffer, &upload_memory);
	destroy_buffer(vulkan, &device_buffer, &device_memory);
	test_done(vulkan);
	return 0;
}
