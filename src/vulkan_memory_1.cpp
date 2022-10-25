#include "vulkan_common.h"

static int num_buffers = 48;
static int loops = 1;
static bool apprename = false;

static void show_usage()
{
	printf("-b/--buffers N         Set number of buffers to use (default %d)\n", num_buffers);
	printf("-l/--loops N           Set number of loops to run (default %d)\n", loops);
	printf("-N/--rename            Change app name for each loop (default %s)\n", apprename ? "true" : "false");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-b", "--buffers"))
	{
		num_buffers = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-l", "--loops"))
	{
		loops = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-N", "--rename"))
	{
		apprename = true;
		return true;
	}
	return false;
}

static int test(int argc, char** argv, int iteration)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_0;
	std::string testname = "vulkan_memory_1";
	if (apprename) testname += "_i" + std::to_string(iteration);
	vulkan_setup_t vulkan = test_init(argc, argv, testname, reqs);

	VkCommandPool cmdpool;
	VkCommandPoolCreateInfo cmdcreateinfo = {};
	cmdcreateinfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdcreateinfo.flags = 0;
	cmdcreateinfo.queueFamilyIndex = 0;
	VkResult result = vkCreateCommandPool(vulkan.device, &cmdcreateinfo, nullptr, &cmdpool);
	check(result);
	test_set_name(vulkan.device, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)cmdpool, "Our command pool");

	std::vector<VkCommandBuffer> cmdbuffers(10);
	VkCommandBufferAllocateInfo pAllocateInfo = {};
	pAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	pAllocateInfo.commandBufferCount = 10;
	pAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	pAllocateInfo.commandPool = cmdpool;
	pAllocateInfo.pNext = nullptr;
	result = vkAllocateCommandBuffers(vulkan.device, &pAllocateInfo, cmdbuffers.data());
	check(result);

	std::vector<VkBuffer> buffer(num_buffers);
	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = 1024 * 1024;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	for (int i = 0; i < num_buffers; i++)
	{
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer[i]);
	}

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(vulkan.device, buffer[0], &req);
	uint32_t memoryTypeIndex = get_device_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkMemoryAllocateInfo pAllocateMemInfo = {};
	pAllocateMemInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = req.size * num_buffers;
	VkDeviceMemory memory = 0;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	assert(memory != 0);

	VkDeviceSize offset = 0;
	vkBindBufferMemory(vulkan.device, buffer[0], memory, offset); offset += req.size;
	test_set_name(vulkan.device, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer[0], "Very temporary buffer");
	vkBindBufferMemory(vulkan.device, buffer[1], memory, offset); offset += req.size;
	vkDestroyBuffer(vulkan.device, buffer[0], nullptr); buffer[0] = VK_NULL_HANDLE;
	vkBindBufferMemory(vulkan.device, buffer[2], memory, offset); offset += req.size;
	vkBindBufferMemory(vulkan.device, buffer[3], memory, offset); offset += req.size;
	vkBindBufferMemory(vulkan.device, buffer[4], memory, offset); offset += req.size;
	vkBindBufferMemory(vulkan.device, buffer[5], memory, offset); offset += req.size;
	vkDestroyBuffer(vulkan.device, buffer[4], nullptr); buffer[4] = VK_NULL_HANDLE;
	vkBindBufferMemory(vulkan.device, buffer[6], memory, offset); offset += req.size;
	vkBindBufferMemory(vulkan.device, buffer[7], memory, offset); offset += req.size;
	vkBindBufferMemory(vulkan.device, buffer[8], memory, offset); offset += req.size;
	vkBindBufferMemory(vulkan.device, buffer[9], memory, offset); offset += req.size;
	vkDestroyBuffer(vulkan.device, buffer[9], nullptr); buffer[9] = VK_NULL_HANDLE;
	vkBindBufferMemory(vulkan.device, buffer[10], memory, offset); offset += req.size;
	test_set_name(vulkan.device, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer[10], "Buffer 10");
	vkDestroyBuffer(vulkan.device, buffer[1], nullptr); buffer[1] = VK_NULL_HANDLE;

	for (int i = 11; i < num_buffers; i++)
	{
		vkBindBufferMemory(vulkan.device, buffer[i], memory, offset);
		offset += req.size;
	}

	VkDescriptorSetLayoutCreateInfo cdslayout = {};
	cdslayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	cdslayout.bindingCount = 1;
	VkDescriptorSetLayoutBinding dslb = {};
	dslb.binding = 0;
	dslb.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	dslb.descriptorCount = 10;
	dslb.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	dslb.pImmutableSamplers = VK_NULL_HANDLE;
	cdslayout.pBindings = &dslb;
	VkDescriptorSetLayout dslayout;
	result = vkCreateDescriptorSetLayout(vulkan.device, &cdslayout, nullptr, &dslayout);
	assert(result == VK_SUCCESS);

	VkDescriptorPoolCreateInfo cdspool = {};
	cdspool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	cdspool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	cdspool.maxSets = 500;
	cdspool.poolSizeCount = 1;
	VkDescriptorPoolSize dps = {};
	dps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	dps.descriptorCount = 1;
	cdspool.pPoolSizes = &dps;
	VkDescriptorPool pool;
	result = vkCreateDescriptorPool(vulkan.device, &cdspool, nullptr, &pool);
	assert(result == VK_SUCCESS);
	vkResetDescriptorPool(vulkan.device, pool, 0);

	// Cleanup...
	vkDestroyDescriptorPool(vulkan.device, pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, dslayout, nullptr);
	for (int i = 0; i < num_buffers; i++)
	{
		vkDestroyBuffer(vulkan.device, buffer[i], nullptr);
	}

	testFreeMemory(vulkan, memory);
	vkFreeCommandBuffers(vulkan.device, cmdpool, cmdbuffers.size(), cmdbuffers.data());
	vkDestroyCommandPool(vulkan.device, cmdpool, nullptr);

	test_done(vulkan);
	return 0;
}

int main(int argc, char** argv)
{
	for (int i = 0; i < loops; i++) test(argc, argv, i);
}
