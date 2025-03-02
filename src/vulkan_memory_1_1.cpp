#include "vulkan_common.h"

#define NUM_BUFFERS 48

// Written for Vulkan 1.1
int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back("VK_KHR_get_memory_requirements2");
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
	assert(reqkhr.memoryRequirements.memoryTypeBits == req.memoryTypeBits);
	assert(reqkhr.memoryRequirements.size == req.size);
	assert(reqkhr.memoryRequirements.alignment == req.alignment);

	MAKEDEVICEPROCADDR(vulkan, vkGetDeviceBufferMemoryRequirementsKHR);
	VkDeviceBufferMemoryRequirementsKHR reqinfokhrmaint4 = { VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS_KHR, nullptr };
	reqinfokhrmaint4.pCreateInfo = &bufferCreateInfo;
	VkMemoryRequirements2KHR reqkhrmaint4 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR, nullptr };
	pf_vkGetDeviceBufferMemoryRequirementsKHR(vulkan.device, &reqinfokhrmaint4, &reqkhrmaint4);
	assert(reqkhrmaint4.memoryRequirements.memoryTypeBits == req.memoryTypeBits);
	assert(reqkhrmaint4.memoryRequirements.size == req.size);
	assert(reqkhrmaint4.memoryRequirements.alignment == req.alignment);

	if (reqs.apiVersion >= VK_API_VERSION_1_3)
	{
		VkDeviceBufferMemoryRequirements reqinfo13 = { VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS, nullptr };
		reqinfo13.pCreateInfo = &bufferCreateInfo;
		VkMemoryRequirements2 req13 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
		vkGetDeviceBufferMemoryRequirements(vulkan.device, &reqinfo13, &req13);
		assert(req13.memoryRequirements.memoryTypeBits == req.memoryTypeBits);
		assert(req13.memoryRequirements.size == req.size);
		assert(req13.memoryRequirements.alignment == req.alignment);
	}

	const uint32_t memoryTypeIndex = get_device_memory_type(reqkhr.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = req.size * NUM_BUFFERS;
	VkDeviceMemory memory = 0;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	assert(memory != 0);

	testBindBufferMemory(vulkan, buffer, memory, req.size);

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
