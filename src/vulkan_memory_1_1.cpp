#include "vulkan_common.h"

#define NUM_BUFFERS 48

static void assert_memory_requirements_equal(const VkMemoryRequirements& lhs, const VkMemoryRequirements& rhs)
{
	assert(lhs.memoryTypeBits == rhs.memoryTypeBits);
	assert(lhs.size == rhs.size);
	assert(lhs.alignment == rhs.alignment);
}

// Written for Vulkan 1.1
int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.maxApiVersion = VK_API_VERSION_1_3;
	reqs.device_extensions.push_back("VK_KHR_get_memory_requirements2");
	reqs.device_extensions.push_back("VK_KHR_map_memory2");
	reqs.device_extensions.push_back("VK_KHR_maintenance1");
	reqs.device_extensions.push_back("VK_KHR_maintenance2");
	reqs.device_extensions.push_back("VK_KHR_maintenance3");
	reqs.device_extensions.push_back("VK_KHR_maintenance4");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_memory_1_1", reqs);

	VkCommandPool cmdpool;
	VkCommandPoolCreateInfo cmdcreateinfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	cmdcreateinfo.flags = 0;
	cmdcreateinfo.queueFamilyIndex = 0;
	VkResult result = vkCreateCommandPool(vulkan.device, &cmdcreateinfo, nullptr, &cmdpool);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)cmdpool, "Our command pool");

	std::vector<VkCommandBuffer> cmdbuffers(10);
	VkCommandBufferAllocateInfo pAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	pAllocateInfo.commandBufferCount = 10;
	pAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	pAllocateInfo.commandPool = cmdpool;
	pAllocateInfo.pNext = nullptr;
	result = vkAllocateCommandBuffers(vulkan.device, &pAllocateInfo, cmdbuffers.data());
	check(result);

	std::vector<VkBuffer> buffer(NUM_BUFFERS);
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = 1024 * 1024;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	for (unsigned i = 0; i < NUM_BUFFERS; i++)
	{
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer[i]);
		check(result);
		test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer[i], "A buffer");
		test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer[i], "B for buffer");
	}

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(vulkan.device, buffer[0], &req);

	VkBufferMemoryRequirementsInfo2 reqinfo = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2, nullptr, buffer[0] };
	VkMemoryRequirements2 reqnew = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
	vkGetBufferMemoryRequirements2(vulkan.device, &reqinfo, &reqnew);
	assert(req.memoryTypeBits == reqnew.memoryRequirements.memoryTypeBits);
	assert(req.size == reqnew.memoryRequirements.size);
	assert(req.alignment == reqnew.memoryRequirements.alignment);

	MAKEDEVICEPROCADDR(vulkan, vkGetBufferMemoryRequirements2KHR);
	VkBufferMemoryRequirementsInfo2KHR reqinfokhr = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2_KHR, nullptr, buffer[0] };
	VkMemoryRequirements2KHR reqkhr = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR, nullptr };
	pf_vkGetBufferMemoryRequirements2KHR(vulkan.device, &reqinfokhr, &reqkhr);
	assert_memory_requirements_equal(reqkhr.memoryRequirements, req);

	MAKEDEVICEPROCADDR(vulkan, vkGetDeviceBufferMemoryRequirementsKHR);
	VkDeviceBufferMemoryRequirementsKHR reqinfokhrmaint4 = { VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS_KHR, nullptr };
	reqinfokhrmaint4.pCreateInfo = &bufferCreateInfo;
	VkMemoryRequirements2KHR reqkhrmaint4 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR, nullptr };
	pf_vkGetDeviceBufferMemoryRequirementsKHR(vulkan.device, &reqinfokhrmaint4, &reqkhrmaint4);
	assert_memory_requirements_equal(reqkhrmaint4.memoryRequirements, req);

	if (reqs.apiVersion >= VK_API_VERSION_1_3)
	{
		VkDeviceBufferMemoryRequirements reqinfo13 = { VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS, nullptr };
		reqinfo13.pCreateInfo = &bufferCreateInfo;
		VkMemoryRequirements2 req13 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
		vkGetDeviceBufferMemoryRequirements(vulkan.device, &reqinfo13, &req13);
		assert_memory_requirements_equal(req13.memoryRequirements, req);
	}

	VkImage image = VK_NULL_HANDLE;
	VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_create_info.extent = { 64, 64, 1 };
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(vulkan.device, &image_create_info, nullptr, &image);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)image, "Regular image");

	VkMemoryRequirements image_requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, image, &image_requirements);
	VkImageMemoryRequirementsInfo2 image_requirements_info = {
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, nullptr, image
	};
	VkMemoryRequirements2 image_requirements2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
	vkGetImageMemoryRequirements2(vulkan.device, &image_requirements_info, &image_requirements2);
	assert_memory_requirements_equal(image_requirements2.memoryRequirements, image_requirements);

	MAKEDEVICEPROCADDR(vulkan, vkGetImageMemoryRequirements2KHR);
	VkImageMemoryRequirementsInfo2KHR image_requirements_info_khr = {
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR, nullptr, image
	};
	VkMemoryRequirements2KHR image_requirements2_khr = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR, nullptr };
	pf_vkGetImageMemoryRequirements2KHR(vulkan.device, &image_requirements_info_khr, &image_requirements2_khr);
	assert_memory_requirements_equal(image_requirements2_khr.memoryRequirements, image_requirements);

	const uint32_t memoryTypeIndex = get_device_memory_type(reqkhr.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = req.size * NUM_BUFFERS;
	VkDeviceMemory memory = 0;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	assert(memory != 0);

	testBindBufferMemory(vulkan, buffer, memory, req.size);

	const VkDeviceSize map_offset = req.size * (NUM_BUFFERS / 2);
	uint8_t* mapped = nullptr;

	MAKEDEVICEPROCADDR(vulkan, vkMapMemory2KHR);
	VkMemoryMapInfoKHR map_info = { VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR, nullptr };
	map_info.flags = 0;
	map_info.memory = memory;
	map_info.offset = map_offset;
	map_info.size = req.size;
	result = pf_vkMapMemory2KHR(vulkan.device, &map_info, (void**)&mapped);
	check(result);
	assert(mapped);
	memset(mapped, 0x5a, req.size);

	MAKEDEVICEPROCADDR(vulkan, vkUnmapMemory2KHR);
	VkMemoryUnmapInfoKHR unmap_info = { VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR, nullptr };
	unmap_info.flags = 0;
	unmap_info.memory = memory;
	result = pf_vkUnmapMemory2KHR(vulkan.device, &unmap_info);
	check(result);

	VkDescriptorSetLayoutCreateInfo cdslayout = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	cdslayout.bindingCount = 1;
	VkDescriptorSetLayoutBinding dslb = {};
	dslb.binding = 0;
	dslb.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	dslb.descriptorCount = 10;
	dslb.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	dslb.pImmutableSamplers = VK_NULL_HANDLE;
	cdslayout.pBindings = &dslb;
	VkDescriptorSetLayoutSupport support = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT, nullptr };
	support.supported = VK_FALSE;
	vkGetDescriptorSetLayoutSupport(vulkan.device, &cdslayout, &support);
	assert(support.supported == VK_TRUE);
	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorSetLayoutSupportKHR);
	support.supported = VK_FALSE;
	pf_vkGetDescriptorSetLayoutSupportKHR(vulkan.device, &cdslayout, &support);
	assert(support.supported == VK_TRUE);
	VkDescriptorSetLayout dslayout;
	result = vkCreateDescriptorSetLayout(vulkan.device, &cdslayout, nullptr, &dslayout);
	assert(result == VK_SUCCESS);

	VkDescriptorPoolCreateInfo cdspool = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	cdspool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	cdspool.maxSets = 50;
	cdspool.poolSizeCount = 1;
	VkDescriptorPoolSize dps = {};
	dps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	dps.descriptorCount = 1;
	cdspool.pPoolSizes = &dps;
	VkDescriptorPool pool;
	result = vkCreateDescriptorPool(vulkan.device, &cdspool, nullptr, &pool);
	assert(result == VK_SUCCESS);
	vkResetDescriptorPool(vulkan.device, pool, 0);

	vkTrimCommandPool(vulkan.device, cmdpool, 0);

	MAKEDEVICEPROCADDR(vulkan, vkTrimCommandPoolKHR);
	pf_vkTrimCommandPoolKHR(vulkan.device, cmdpool, 0);

	// Cleanup...
	vkDestroyDescriptorPool(vulkan.device, pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, dslayout, nullptr);
	vkDestroyImage(vulkan.device, image, nullptr);
	for (unsigned i = 0; i < NUM_BUFFERS; i++)
	{
		vkDestroyBuffer(vulkan.device, buffer[i], nullptr);
	}

	testFreeMemory(vulkan, memory);
	vkFreeCommandBuffers(vulkan.device, cmdpool, cmdbuffers.size(), cmdbuffers.data());
	vkDestroyCommandPool(vulkan.device, cmdpool, nullptr);

	test_done(vulkan);
	return 0;
}
