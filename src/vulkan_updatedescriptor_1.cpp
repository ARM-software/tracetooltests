#include "vulkan_common.h"

static int num_buffers = 48;

static void show_usage()
{
	printf("-b/--buffers N         Set number of buffers to use (default %d)\n", num_buffers);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-b", "--buffers"))
	{
		num_buffers = get_arg(argv, ++i, argc);
		return true;
	}
	return false;
}

static int test(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_1;
	std::string testname = "vulkan_updatedescriptor_1";
	vulkan_setup_t vulkan = test_init(argc, argv, testname, reqs);

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

	std::vector<VkBuffer> buffers(num_buffers);
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = 1024;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vulkan.garbage_pointers)
	{
		bufferCreateInfo.queueFamilyIndexCount = 100;
		bufferCreateInfo.pQueueFamilyIndices = (const uint32_t*)0xdeadbeef;
	}
	for (int i = 0; i < num_buffers; i++)
	{
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffers[i]);
	}
	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(vulkan.device, buffers[0], &req);
	uint32_t memoryTypeIndex = get_device_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = req.size * num_buffers;
	VkDeviceMemory memory = 0;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	assert(memory != 0);
	VkDeviceSize offset = 0;
	for (int i = 0; i < num_buffers; i++)
	{
		vkBindBufferMemory(vulkan.device, buffers[i], memory, offset);
		offset += req.size;
	}

	VkDescriptorSetLayoutCreateInfo cdslayout = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	cdslayout.bindingCount = num_buffers;
	std::vector<VkDescriptorSetLayoutBinding> dslb(num_buffers);
	for (int i = 0; i < num_buffers; i++)
	{
		dslb[i].binding = i;
		dslb[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		dslb[i].descriptorCount = 1;
		dslb[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		dslb[i].pImmutableSamplers = VK_NULL_HANDLE;
	}
	cdslayout.pBindings = dslb.data();
	VkDescriptorSetLayout dslayout;
	result = vkCreateDescriptorSetLayout(vulkan.device, &cdslayout, nullptr, &dslayout);
	check(result);

	VkDescriptorPoolCreateInfo cdspool = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	cdspool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	cdspool.maxSets = num_buffers * 2;
	cdspool.poolSizeCount = 1;
	VkDescriptorPoolSize dps;
	dps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	dps.descriptorCount = num_buffers * 2;
	cdspool.pPoolSizes = &dps;
	VkDescriptorPool pool;
	result = vkCreateDescriptorPool(vulkan.device, &cdspool, nullptr, &pool);
	check(result);
	vkResetDescriptorPool(vulkan.device, pool, 0);

	VkDescriptorSet descriptorset = VK_NULL_HANDLE;
	VkDescriptorSet target_descriptorset = VK_NULL_HANDLE;
	VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	dsai.descriptorPool = pool;
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts = &dslayout;
	result = vkAllocateDescriptorSets(vulkan.device, &dsai, &descriptorset);
	result = vkAllocateDescriptorSets(vulkan.device, &dsai, &target_descriptorset);
	check(result);
	assert(descriptorset != VK_NULL_HANDLE);

	std::vector<VkWriteDescriptorSet> descriptorsetwrites(num_buffers);
	std::vector<VkCopyDescriptorSet> descriptorsetcopies(num_buffers);
	std::vector<VkDescriptorBufferInfo> descbufinfo(num_buffers);
	for (int i = 0; i < num_buffers; i++)
	{
		descbufinfo[i].buffer = buffers.at(i);
		descbufinfo[i].offset = 0;
		descbufinfo[i].range = VK_WHOLE_SIZE;
	}

	for (int i = 0; i < num_buffers; i++)
	{
		descriptorsetwrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorsetwrites[i].pNext = nullptr;
		descriptorsetwrites[i].dstSet = descriptorset;
		descriptorsetwrites[i].dstBinding = i;
		descriptorsetwrites[i].dstArrayElement = 0;
		descriptorsetwrites[i].descriptorCount = 1;
		descriptorsetwrites[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorsetwrites[i].pBufferInfo = &descbufinfo.at(i);
		if (vulkan.garbage_pointers)
		{
			descriptorsetwrites[i].pImageInfo = (const VkDescriptorImageInfo*)0xdeadbeef;
			descriptorsetwrites[i].pTexelBufferView = (const VkBufferView*)0xdeadbeef;
		}
		else
		{
			descriptorsetwrites[i].pImageInfo = nullptr;
			descriptorsetwrites[i].pTexelBufferView = nullptr;
		}
		descriptorsetcopies[i].sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
		descriptorsetcopies[i].pNext = nullptr;
		descriptorsetcopies[i].srcSet = descriptorset;
		descriptorsetcopies[i].dstSet = target_descriptorset;
		descriptorsetcopies[i].srcBinding = i;
		descriptorsetcopies[i].srcArrayElement = 0;
		descriptorsetcopies[i].dstBinding = i;
		descriptorsetcopies[i].dstArrayElement = 0;
		descriptorsetcopies[i].descriptorCount = 1;
	}

	// Dummy call.
	vkUpdateDescriptorSets(vulkan.device, 0, nullptr, 0, nullptr);

	// This is insane but allowed...
	// "Copying a descriptor from a descriptor set does not constitute a use of the referenced resource or view,
	// as it is the reference itself that is copied. Applications can copy a descriptor referencing a destroyed
	// resource, and it can copy an undefined descriptor. The destination descriptor becomes undefined in both cases."
	vkUpdateDescriptorSets(vulkan.device, 0, nullptr, num_buffers, descriptorsetcopies.data());

	// Good call
	vkUpdateDescriptorSets(vulkan.device, num_buffers, descriptorsetwrites.data(), num_buffers, descriptorsetcopies.data());

	// Cleanup...
	result = vkFreeDescriptorSets(vulkan.device, pool, 1, &descriptorset);
	result = vkFreeDescriptorSets(vulkan.device, pool, 1, &target_descriptorset);
	check(result);
	vkDestroyDescriptorPool(vulkan.device, pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, dslayout, nullptr);
	for (int i = 0; i < num_buffers; i++)
	{
		vkDestroyBuffer(vulkan.device, buffers.at(i), nullptr);
	}
	testFreeMemory(vulkan, memory);
	vkFreeCommandBuffers(vulkan.device, cmdpool, cmdbuffers.size(), cmdbuffers.data());
	vkDestroyCommandPool(vulkan.device, cmdpool, nullptr);

	test_done(vulkan);
	return 0;
}

int main(int argc, char** argv)
{
	test(argc, argv);
}
