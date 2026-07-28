#include "vulkan_common.h"

static uint32_t bind_count = 1;

static void show_usage()
{
	printf("-bc/--bind-count N     Number of buffer and image memory bindings per call (default %u)\n", bind_count);
}

static VkDeviceMemory allocate_memory(
	const vulkan_setup_t &vulkan,
	const VkMemoryRequirements &requirements,
	VkMemoryPropertyFlags flags)
{
	VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	allocate_info.allocationSize = requirements.size;

	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, flags);

	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &memory);
	check(result);
	assert(memory != VK_NULL_HANDLE);
	return memory;
}

static bool test_cmdopt(int &i, int argc, char **argv, vulkan_req_t &reqs)
{
	(void)reqs;

	if (match(argv[i], "-bc", "--bind-count"))
	{
		const int value = get_arg(argv, ++i, argc);
		if (value <= 0)
		{
			return false;
		}
		bind_count = static_cast<uint32_t>(value);
		return true;
	}
	return false;
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_0;
	reqs.minApiVersion = VK_API_VERSION_1_0;
	reqs.maxApiVersion = VK_API_VERSION_1_0;
	reqs.device_extensions.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	reqs.cmdopt = test_cmdopt;
	reqs.usage = show_usage;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_bind_memory2", reqs);
	assert(vulkan.device_extensions.count(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME) == 1);

	MAKEDEVICEPROCADDR(vulkan, vkBindBufferMemory2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkBindImageMemory2KHR);

	std::vector<VkBuffer> buffers(bind_count);
	std::vector<VkDeviceMemory> buffer_memories(bind_count);
	std::vector<VkDeviceSize> buffer_sizes(bind_count);
	std::vector<VkBindBufferMemoryInfoKHR> bind_buffer_infos(bind_count);
	std::vector<uint32_t> expected_crc(bind_count);
	VkResult result;
	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	bench_start_iteration(vulkan.bench);

	for (uint32_t i = 0; i < bind_count; i++)
	{
		buffer_sizes[i] = 512 * (i + 1);

		VkBufferCreateInfo buffer_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
		buffer_create_info.size = buffer_sizes[i];
		buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		result = vkCreateBuffer(vulkan.device, &buffer_create_info, nullptr, &buffers[i]);
		check(result);

		std::string name = std::string("bind_memory2_buffer_") + std::to_string(i);
		test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffers[i], name.c_str());

		VkMemoryRequirements req = {};
		vkGetBufferMemoryRequirements(vulkan.device, buffers[i], &req);

		buffer_memories[i] = allocate_memory(vulkan, req,
											 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		bind_buffer_infos[i] = {VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO_KHR, nullptr};
		bind_buffer_infos[i].buffer = buffers[i];
		bind_buffer_infos[i].memory = buffer_memories[i];
		bind_buffer_infos[i].memoryOffset = 0;
	}

	result = pf_vkBindBufferMemory2KHR(vulkan.device, bind_count, bind_buffer_infos.data());
	check(result);

	for (uint32_t i = 0; i < bind_count; i++)
	{
		test_marker_mention(vulkan, "Executed vkBindBufferMemory2KHR", VK_OBJECT_TYPE_BUFFER, (uint64_t)buffers[i]);
	}

	std::vector<VkImage> images(bind_count);
	std::vector<VkDeviceMemory> image_memories(bind_count);
	std::vector<VkBindImageMemoryInfoKHR> bind_image_infos(bind_count);

	for (uint32_t i = 0; i < bind_count; i++)
	{
		VkImageCreateInfo image_create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
		image_create_info.imageType = VK_IMAGE_TYPE_2D;
		image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
		image_create_info.extent = {16, 16, 1};
		image_create_info.mipLevels = 1;
		image_create_info.arrayLayers = 1;
		image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		result = vkCreateImage(vulkan.device, &image_create_info, nullptr, &images[i]);
		check(result);

		std::string name = std::string("bind_memory2_image_") + std::to_string(i);
		test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)images[i], name.c_str());

		VkMemoryRequirements req = {};
		vkGetImageMemoryRequirements(vulkan.device, images[i], &req);

		image_memories[i] = allocate_memory(vulkan, req, 0);

		bind_image_infos[i] = {VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO_KHR, nullptr};
		bind_image_infos[i].image = images[i];
		bind_image_infos[i].memory = image_memories[i];
		bind_image_infos[i].memoryOffset = 0;
	}

	result = pf_vkBindImageMemory2KHR(vulkan.device, bind_count, bind_image_infos.data());
	check(result);

	for (uint32_t i = 0; i < bind_count; i++)
	{
		test_marker_mention(vulkan, "Executed vkBindImageMemory2KHR", VK_OBJECT_TYPE_IMAGE, (uint64_t)images[i]);
	}

	for (uint32_t i = 0; i < bind_count; i++)
	{
		char *data = nullptr;
		result = vkMapMemory(vulkan.device, buffer_memories[i], 0, buffer_sizes[i], 0, (void **)&data);
		check(result);
		memset(data, 0x11 * (i + 1), buffer_sizes[i]);
		expected_crc[i] = adler32((unsigned char *)data, buffer_sizes[i]);

		if (vulkan.has_explicit_host_updates)
		{
			testFlushMemory(vulkan, buffer_memories[i], 0, buffer_sizes[i], true);
		}
		vkUnmapMemory(vulkan.device, buffer_memories[i]);
	}

	testQueueBuffer(vulkan, queue, buffers);
	if (vulkan.vkAssertBuffer)
	{
		for (uint32_t i = 0; i < bind_count; i++)
		{
			uint32_t buffer_crc = 0;
			const VkUpdateBufferInfoARM buffer_info = {
				VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM,
				nullptr,
				buffers[i],
				0,
				VK_WHOLE_SIZE,
				nullptr};
			char name[32];
			snprintf(name, sizeof(name), "bind_memory2_buffer_%u", i);

			result = vulkan.vkAssertBuffer(vulkan.device, &buffer_info, &buffer_crc, name);
			check(result);

			if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
			{
				assert(buffer_crc == expected_crc[i]);
			}
		}
	}

	bench_stop_iteration(vulkan.bench);

	for (uint32_t i = 0; i < bind_count; i++)
	{
		vkDestroyImage(vulkan.device, images[i], nullptr);
		vkFreeMemory(vulkan.device, image_memories[i], nullptr);
		vkDestroyBuffer(vulkan.device, buffers[i], nullptr);
		vkFreeMemory(vulkan.device, buffer_memories[i], nullptr);
	}
	test_done(vulkan);
	return 0;
}
