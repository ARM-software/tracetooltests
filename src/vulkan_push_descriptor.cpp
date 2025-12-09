#include "vulkan_common.h"

#include <vector>

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)i;
	(void)argc;
	(void)argv;
	(void)reqs;
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back("VK_KHR_push_descriptor");
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	auto vk = test_init(argc, argv, "vulkan_push_descriptor", reqs);

	MAKEDEVICEPROCADDR(vk, vkCmdPushDescriptorSetKHR);

	bench_start_iteration(vk.bench);

	// Create a small uniform buffer to push as a descriptor
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bci.size = 256;
	bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	check(vkCreateBuffer(vk.device, &bci, nullptr, &buffer));

	VkMemoryRequirements memreq{};
	vkGetBufferMemoryRequirements(vk.device, buffer, &memreq);
	VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	mai.allocationSize = memreq.size;
	mai.memoryTypeIndex = get_device_memory_type(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	check(vkAllocateMemory(vk.device, &mai, nullptr, &memory));
	check(vkBindBufferMemory(vk.device, buffer, memory, 0));

	// Set up a push-descriptor layout and pipeline layout
	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	dslci.bindingCount = 1;
	dslci.pBindings = &binding;

	VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
	check(vkCreateDescriptorSetLayout(vk.device, &dslci, nullptr, &dsl));

	VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &dsl;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	check(vkCreatePipelineLayout(vk.device, &plci, nullptr, &layout));

	// Command buffer to record the push descriptor call
	VkCommandPool cmdpool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo cpci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	cpci.queueFamilyIndex = 0;
	check(vkCreateCommandPool(vk.device, &cpci, nullptr, &cmdpool));

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	cbai.commandPool = cmdpool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;
	check(vkAllocateCommandBuffers(vk.device, &cbai, &cmd));

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(vkBeginCommandBuffer(cmd, &beginInfo));

	VkDescriptorBufferInfo dbi{};
	dbi.buffer = buffer;
	dbi.offset = 0;
	dbi.range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	write.pBufferInfo = &dbi;

	pf_vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &write);

	check(vkEndCommandBuffer(cmd));

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vk.device, 0, 0, &queue);

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	check(vkCreateFence(vk.device, &fci, nullptr, &fence));

	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	check(vkQueueSubmit(queue, 1, &submitInfo, fence));
	check(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));

	bench_stop_iteration(vk.bench);

	// Clean up
	vkDestroyFence(vk.device, fence, nullptr);
	vkFreeCommandBuffers(vk.device, cmdpool, 1, &cmd);
	vkDestroyCommandPool(vk.device, cmdpool, nullptr);
	vkDestroyPipelineLayout(vk.device, layout, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, dsl, nullptr);
	vkDestroyBuffer(vk.device, buffer, nullptr);
	testFreeMemory(vk, memory);

	test_done(vk);
	return 0;
}
