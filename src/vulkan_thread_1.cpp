#include <atomic>
#include <thread>

#include "vulkan_common.h"

#define NUM_BUFFERS 48

#define THREADS 20
static std::atomic_int used[THREADS + 1];
static vulkan_setup_t vulkan;

void usage()
{
}

static void thread_test_stress(int tid)
{
	if (random() % 5 == 1) usleep(random() % 3 * 10000); // introduce some pseudo-random timings
	set_thread_name("stress thread");
	assert(tid < THREADS + 1);
	assert(used[tid] == 0);
	used[tid] = 1;
	assert(vulkan.device != VK_NULL_HANDLE);

	VkCommandPool cmdpool;
	VkCommandPoolCreateInfo cmdcreateinfo = {};
	cmdcreateinfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdcreateinfo.flags = 0;
	cmdcreateinfo.queueFamilyIndex = 0;
	VkResult result = vkCreateCommandPool(vulkan.device, &cmdcreateinfo, nullptr, &cmdpool);
	check(result);
	std::string tmpstr = "Our temporary command pool for tid " + _to_string(tid);
	test_set_name(vulkan.device, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)cmdpool, tmpstr.c_str());

	if (random() % 5 == 1) usleep(random() % 3 * 10000); // introduce some pseudo-random timings

	std::vector<VkCommandBuffer> cmdbuffers(10);
	VkCommandBufferAllocateInfo pAllocateInfo = {};
	pAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	pAllocateInfo.commandBufferCount = 10;
	pAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	pAllocateInfo.commandPool = cmdpool;
	pAllocateInfo.pNext = nullptr;
	result = vkAllocateCommandBuffers(vulkan.device, &pAllocateInfo, cmdbuffers.data());
	check(result);

	if (random() % 5 == 1) usleep(random() % 3 * 10000); // introduce some pseudo-random timings

	vkFreeCommandBuffers(vulkan.device, cmdpool, cmdbuffers.size(), cmdbuffers.data());
	vkDestroyCommandPool(vulkan.device, cmdpool, nullptr);

	if (random() % 5 == 1) usleep(random() % 3 * 10000); // introduce some pseudo-random timings
}

int main()
{
	vulkan = test_init("vulkan_thread_1");
	std::vector<std::thread*> threads(THREADS);
	int i = 0;
	for (auto& t : threads)
	{
		t = new std::thread(thread_test_stress, i);
		i++;
	}
	for (std::thread* t : threads)
	{
		t->join();
		delete t;
	}
	threads.clear();
	test_done(vulkan);
	return 0;
}
