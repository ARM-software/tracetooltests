// Test intermixing different types of content in the same buffer

#include "vulkan_common.h"

static int offset = 0;
static bool dev_address = false;
static bool raytracing = false;

static void show_usage()
{
	printf("-O / --offset N      Add an offset to the buffer (default %d)\n", offset);
	printf("-R / --raytracing    Add raytracing to the test\n");
	printf("-A / --dev-address   Add device addresses to the test\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-O", "--offset"))
	{
		offset = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-R", "--raytracing"))
	{
		raytracing = true;
		reqs.device_extensions.push_back("VK_KHR_shader_float_controls");
		reqs.device_extensions.push_back("VK_KHR_spirv_1_4");
		reqs.device_extensions.push_back("VK_KHR_buffer_device_address");
		reqs.device_extensions.push_back("VK_EXT_descriptor_indexing");
		reqs.device_extensions.push_back("VK_KHR_deferred_host_operations");
		reqs.device_extensions.push_back("VK_KHR_acceleration_structure");
		reqs.device_extensions.push_back("VK_KHR_ray_tracing_pipeline");
		return true;
	}
	else if (match(argv[i], "-A", "--dev-address"))
	{
		dev_address = true;
		reqs.device_extensions.push_back("VK_EXT_buffer_device_address");
		return true;
	}
	return false;
}

struct buffer_info
{
	VkBuffer buffer;
	VkMemoryRequirements memory_info;
	uint32_t memory_type;
	uint32_t memory_offset;
};

buffer_info make_buffer(const vulkan_setup_t& vulkan, const char* name, VkBufferUsageFlags bits, VkMemoryPropertyFlagBits memflag)
{
	buffer_info info = {};
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = 1024 * 1024;
	bufferCreateInfo.usage = bits;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &info.buffer);
	check(result);

	vkGetBufferMemoryRequirements(vulkan.device, info.buffer, &info.memory_info);
	info.memory_type = get_device_memory_type(info.memory_info.memoryTypeBits, memflag);
	ILOG("Creating %s buffer, requires memory type %u with alignment %u", name, (unsigned)info.memory_type, (unsigned)info.memory_info.alignment);

	return info;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_1;
	std::string testname = "vulkan_memory_2_1";
	vulkan_setup_t vulkan = test_init(argc, argv, testname, reqs);
	VkQueue queue;

	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	std::vector<buffer_info> buffers;
	if (raytracing)
	{
		buffers.push_back(make_buffer(vulkan, "Acceleration structure", VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
		buffers.push_back(make_buffer(vulkan, "Shader Binding Table", VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	}
	if (dev_address)
	{
		buffers.push_back(make_buffer(vulkan, "Storage + Buffer Device Address", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	}
	buffers.push_back(make_buffer(vulkan, "Transfer source", VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	buffers.push_back(make_buffer(vulkan, "Transfer destination", VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	buffers.push_back(make_buffer(vulkan, "Uniform texel", VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	buffers.push_back(make_buffer(vulkan, "Storage texel", VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	buffers.push_back(make_buffer(vulkan, "Uniform", VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	buffers.push_back(make_buffer(vulkan, "Storage", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	buffers.push_back(make_buffer(vulkan, "Index", VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	buffers.push_back(make_buffer(vulkan, "Vertex", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	buffers.push_back(make_buffer(vulkan, "Indirect GPU", VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

	std::unordered_map<uint32_t, VkDeviceMemory> memory_pools;
	for (unsigned i = 0; i < buffers.size() - 1; i++)
	{
		memory_pools[i] = VK_NULL_HANDLE;
	}
	ILOG("Created %u memory pools", (unsigned)memory_pools.size());

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.allocationSize = offset;
	pAllocateMemInfo.memoryTypeIndex = buffers[0].memory_type;
	for (unsigned i = 0; i < buffers.size(); i++)
	{
		pAllocateMemInfo.allocationSize = aligned_size(pAllocateMemInfo.allocationSize, buffers[i].memory_info.alignment);
		buffers[i].memory_offset = pAllocateMemInfo.allocationSize;
		pAllocateMemInfo.allocationSize += buffers[i].memory_info.size;
	}
	ILOG("Total memory consumption: %u", (unsigned)pAllocateMemInfo.allocationSize);
	for (auto& pair : memory_pools)
	{
		VkResult result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &pair.second);
		assert(pair.second != 0);
		check(result);
	}

	std::vector<VkBuffer> buffer_list;
	for (unsigned i = 0; i < buffers.size(); i++)
	{
		VkDeviceMemory memory = memory_pools.at(buffers[i].memory_type);
		vkBindBufferMemory(vulkan.device, buffers[i].buffer, memory, buffers[i].memory_offset);
		buffer_list.push_back(buffers[i].buffer);
	}

	// Do some dummy workload here
	testQueueBuffer(vulkan, queue, buffer_list);

	for (unsigned i = 0; i < buffers.size(); i++)
	{
		vkDestroyBuffer(vulkan.device, buffers[i].buffer, nullptr);
	}

	for (auto& pair : memory_pools)
	{
		testFreeMemory(vulkan, pair.second);
	}

	test_done(vulkan);

	return 0;
}
