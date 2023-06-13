#include <sys/mman.h>

#include "vulkan_common.h"

static __attribute__((const)) inline uint64_t aligned_size(uint64_t size, uint64_t alignment) { return size + alignment - 1ull - (size + alignment - 1ull) % alignment; }
static __attribute__((const)) inline uint64_t aligned_start(uint64_t size, uint64_t alignment) { return (size & ~(alignment - 1)); }

static VkPhysicalDeviceMemoryProperties memory_properties = {};
static int totalsize = -1;

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static void test_1_each(vulkan_setup_t& vulkan, uint32_t memoryTypeIndex, uint32_t mapsize, uint32_t offset, VkDeviceMemory memory)
{
	printf("test memtype=%u size=%d offset=%u mapsize=%u\n", (unsigned)memoryTypeIndex, totalsize, (unsigned)offset, (unsigned)mapsize);

	char* data = nullptr;
	VkResult result = vkMapMemory(vulkan.device, memory, mapsize, offset, 0, (void**)&data);
	assert(result == VK_SUCCESS);

	int r = mprotect((void*)aligned_start((uint64_t)data, getpagesize()), aligned_size(mapsize, getpagesize()), PROT_READ);
	assert(r == 0);
	r = mprotect((void*)aligned_start((uint64_t)data, getpagesize()), aligned_size(mapsize, getpagesize()), PROT_READ | PROT_WRITE);
	assert(r == 0);

	vkUnmapMemory(vulkan.device, memory);
}

static VkDeviceMemory make_memory(vulkan_setup_t& vulkan, uint32_t memoryTypeIndex, uint32_t size)
{
	VkMemoryAllocateInfo pAllocateMemInfo = {};
	pAllocateMemInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = size;
	VkDeviceMemory memory = 0;
	VkResult result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	assert(result == VK_SUCCESS);
	assert(memory != 0);
	totalsize = size;
	return memory;
}

static void test_1(vulkan_setup_t& vulkan, uint32_t memoryTypeIndex)
{
	VkDeviceMemory memory = make_memory(vulkan, memoryTypeIndex, 1024);
	test_1_each(vulkan, memoryTypeIndex, 1024, 0, memory);
	test_1_each(vulkan, memoryTypeIndex, 1024, 1024, memory);
	test_1_each(vulkan, memoryTypeIndex, 512, 1, memory);
	test_1_each(vulkan, memoryTypeIndex, 10, 512, memory);
	testFreeMemory(vulkan, memory);

	memory = make_memory(vulkan, memoryTypeIndex, 4000);
	test_1_each(vulkan, memoryTypeIndex, 64, 500, memory);
	testFreeMemory(vulkan, memory);

	memory = make_memory(vulkan, memoryTypeIndex, 50);
	test_1_each(vulkan, memoryTypeIndex, 5, 0, memory);
	test_1_each(vulkan, memoryTypeIndex, 5, 25, memory);
	testFreeMemory(vulkan, memory);
}

static int test(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_1;
	std::string testname = "vulkan_memory_3";
	vulkan_setup_t vulkan = test_init(argc, argv, testname, reqs);

	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 1)
	{
		VkPhysicalDeviceMemoryProperties2 mprops = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, nullptr };
		vkGetPhysicalDeviceMemoryProperties2(vulkan.physical, &mprops);
		memory_properties = mprops.memoryProperties; // struct copy
	}
	else
	{
		vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &memory_properties);
	}

	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
	{
		if (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			test_1(vulkan, i);
		}
	}

	test_done(vulkan);
	return 0;
}

int main(int argc, char** argv)
{
	return test(argc, argv);
}
