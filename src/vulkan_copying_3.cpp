// Unit test to try out various ways to copy memory from userspace (and see what works with guard pages)

#include "vulkan_common.h"

static int method = 0;
static unsigned buffer_size = (32 * 1024);
static int times = repeats();

void usage()
{
	printf("Usage: vulkan_copying_1\n");
	printf("-h/--help              This help\n");
	printf("-g/--gpu level N       Select GPU (default 0)\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default %d)\n", p__debug_level);
	printf("-b/--buffer-size N     Set buffer size (default %d)\n", buffer_size);
	printf("-c/--copy-method N     Set copy method (default %d)\n", method);
	printf("\t0 - memset\n");
	printf("\t1 - memcpy\n");
	printf("\t2 - fread\n");
	printf("-t/--times N           Times to repeat (default %d)\n", times);
	exit(-1);
}

static void copying_3()
{
	vulkan_setup_t vulkan = test_init("copying_3");
	VkResult result;

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	VkBuffer buffer;
	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer);
	assert(result == VK_SUCCESS);

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan.device, buffer, &memory_requirements);
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
	const uint32_t aligned_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo pAllocateMemInfo = {};
	pAllocateMemInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_size;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	assert(result == VK_SUCCESS);
	assert(memory != VK_NULL_HANDLE);

	vkBindBufferMemory(vulkan.device, buffer, memory, 0);

	char* data = nullptr;
	result = vkMapMemory(vulkan.device, memory, 0, aligned_size, 0, (void**)&data);
	assert(result == VK_SUCCESS);

	// Herein lies the meat of the test...
	FILE* fp = fopen("/dev/urandom", "r");
	assert(fp);
	std::vector<char> buf(buffer_size);
	size_t r = fread(buf.data(), buffer_size, 1, fp);
	assert(r == 1); // this should not fail
	for (int i = 0; i < times; i++)
	{
		switch (method)
		{
		case 0:
			memset(data, 0, buffer_size);
			break;
		case 1:
			memcpy(data, buf.data(), buffer_size);
			break;
		case 2:
			do {
				r = fread(data, buffer_size, 1, fp);
			} while (r != 1 && (errno == EAGAIN || errno == EWOULDBLOCK));
			assert(r == 1);
			break;
		default:
			assert(false);
			break;
		}
		usleep(10);
	}
	fclose(fp);

	vkUnmapMemory(vulkan.device, memory);

	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	vkFreeMemory(vulkan.device, memory, nullptr);
	test_done(vulkan);
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++)
	{
		if (match(argv[i], "-h", "--help"))
		{
			usage();
		}
		else if (match(argv[i], "-d", "--debug"))
		{
			p__debug_level = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-g", "--gpu"))
		{
			select_gpu(get_arg(argv, ++i, argc));
		}
		else if (match(argv[i], "-b", "--buffer-size"))
		{
			buffer_size = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-t", "--times"))
		{
			times = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-c", "--copy-method"))
		{
			method = get_arg(argv, ++i, argc);
		}
		else
		{
			ELOG("Unrecognized cmd line parameter: %s", argv[i]);
		}
	}
	printf("Running with copy method %d\n", method);
	copying_3();
	return 0;
}
