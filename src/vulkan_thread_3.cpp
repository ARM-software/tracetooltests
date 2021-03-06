// Test for multi-threaded tracers. You'll want to allow multiple threads to record commandbuffers in parallell
// but still need to serialize them when needed.

#include <condition_variable>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_set>

#include "vulkan_common.h"

std::mutex m;
std::condition_variable cv;
std::atomic_bool ready{ false };

static vulkan_setup_t vulkan;
static VkCommandPool pool1;
static VkCommandPool pool2;
static VkCommandBuffer cmd1;
static VkCommandBuffer cmd2;
static VkCommandBuffer cmd2_2;

static void dummy_cmd(VkCommandBuffer cmd)
{
	VkMemoryBarrier memory_barrier = {};
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
}

static void thread_case2()
{
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
	VkResult result = vkEndCommandBuffer(cmd1);
	check(result);
}

static void thread_case3()
{
	VkCommandBufferBeginInfo command_buffer_begin_info = {};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkResult result = vkBeginCommandBuffer(cmd1, &command_buffer_begin_info);
	check(result);
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
}

static void thread_case4()
{
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
}

static void thread_case5()
{
	for (int i = 0; i < 500; i++) dummy_cmd(cmd2);
}

static void thread_case6()
{
	std::unique_lock<std::mutex> lk(m);
	cv.wait(lk, []{ return ready.load(); });

	vkCmdExecuteCommands(cmd1, 1, &cmd2_2);
	for (int i = 0; i < 50; i++) dummy_cmd(cmd1);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	vulkan = test_init(argc, argv, "vulkan_thread_3", reqs);
	std::thread* helper = nullptr;

	VkCommandPoolCreateInfo cmdcreateinfo = {};
	cmdcreateinfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdcreateinfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cmdcreateinfo.queueFamilyIndex = 0;
	VkResult result = vkCreateCommandPool(vulkan.device, &cmdcreateinfo, nullptr, &pool1);
	check(result);
	result = vkCreateCommandPool(vulkan.device, &cmdcreateinfo, nullptr, &pool2);
	check(result);

	VkCommandBufferAllocateInfo pAllocateInfo = {};
	pAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	pAllocateInfo.commandBufferCount = 1;
	pAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	pAllocateInfo.commandPool = pool1;
	pAllocateInfo.pNext = nullptr;
	result = vkAllocateCommandBuffers(vulkan.device, &pAllocateInfo, &cmd1);
	check(result);
	pAllocateInfo.commandPool = pool2;
	result = vkAllocateCommandBuffers(vulkan.device, &pAllocateInfo, &cmd2);
	check(result);
	pAllocateInfo.commandPool = pool2;
	pAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
	result = vkAllocateCommandBuffers(vulkan.device, &pAllocateInfo, &cmd2_2);
	check(result);

	VkCommandBufferBeginInfo command_buffer_begin_info = {};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	// Case #1 : Dummy (single thread)
	result = vkBeginCommandBuffer(cmd1, &command_buffer_begin_info);
	check(result);
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
	result = vkEndCommandBuffer(cmd1);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool1, 0);
	check(result);

	// Case #2 : Start here, finish in thread
	result = vkBeginCommandBuffer(cmd1, &command_buffer_begin_info);
	check(result);
	dummy_cmd(cmd1);
	helper = new std::thread(thread_case2);
	helper->join();
	delete helper;
	helper = nullptr;
	result = vkResetCommandPool(vulkan.device, pool1, 0);
	check(result);

	// Case #3 : Start in thread, finish here
	helper = new std::thread(thread_case3);
	helper->join();
	delete helper;
	helper = nullptr;
	dummy_cmd(cmd1);
	result = vkEndCommandBuffer(cmd1);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool1, 0);
	check(result);

	// Case #4 : Start here, work a bit here, work rest in thread, work a bit here, then finish here
	result = vkBeginCommandBuffer(cmd1, &command_buffer_begin_info);
	check(result);
	dummy_cmd(cmd1);
	helper = new std::thread(thread_case4);
	helper->join();
	delete helper;
	helper = nullptr;
	dummy_cmd(cmd1);
	result = vkEndCommandBuffer(cmd1);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool1, 0);
	check(result);

	// Case #5 : Two racing threads
	result = vkBeginCommandBuffer(cmd1, &command_buffer_begin_info);
	check(result);
	result = vkBeginCommandBuffer(cmd2, &command_buffer_begin_info);
	check(result);
	dummy_cmd(cmd1);
	dummy_cmd(cmd2);
	helper = new std::thread(thread_case5);
	for (int i = 0; i < 500; i++) dummy_cmd(cmd1);
	helper->join();
	delete helper;
	helper = nullptr;
	dummy_cmd(cmd1);
	dummy_cmd(cmd2);
	result = vkEndCommandBuffer(cmd1);
	check(result);
	result = vkEndCommandBuffer(cmd2);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool1, 0);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool2, 0);
	check(result);

	// Case #6 : vkCmdExecuteCommands waiting for other thread
	result = vkBeginCommandBuffer(cmd1, &command_buffer_begin_info);
	check(result);
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	VkCommandBufferInheritanceInfo inhinfo = {};
	inhinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	command_buffer_begin_info.pInheritanceInfo = &inhinfo;
	result = vkBeginCommandBuffer(cmd2_2, &command_buffer_begin_info);
	check(result);
	dummy_cmd(cmd1);
	dummy_cmd(cmd1);
	helper = new std::thread(thread_case6);
	dummy_cmd(cmd2_2);
	dummy_cmd(cmd2_2);
	result = vkEndCommandBuffer(cmd2_2);
	check(result);
	ready.store(true);
	cv.notify_one();
	helper->join();
	delete helper;
	helper = nullptr;
	dummy_cmd(cmd1);
	for (int i = 0; i < 50; i++) dummy_cmd(cmd1);
	result = vkEndCommandBuffer(cmd1);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool1, 0);
	check(result);
	result = vkResetCommandPool(vulkan.device, pool2, 0);
	check(result);
	return 0;

	vkFreeCommandBuffers(vulkan.device, pool1, 1, &cmd1);
	vkFreeCommandBuffers(vulkan.device, pool1, 1, &cmd2);
	vkFreeCommandBuffers(vulkan.device, pool1, 1, &cmd2_2);
	vkDestroyCommandPool(vulkan.device, pool1, nullptr);
	vkDestroyCommandPool(vulkan.device, pool2, nullptr);
	test_done(vulkan);
	return 0;
}
