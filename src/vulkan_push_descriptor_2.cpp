// Test for vkCmdPushDescriptorSetWithTemplate

#include "vulkan_common.h"

#include <cstddef>
#include <vector>

struct push_descriptor_template_element
{
	VkDescriptorBufferInfo descriptor;
	uint64_t padding;
};

struct push_descriptor_template_data
{
	uint64_t prefix;
	push_descriptor_template_element descriptors[2];
	uint64_t suffix;
};

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

	auto vk = test_init(argc, argv, "vulkan_push_descriptor_2", reqs);

#ifdef VULKAN_1_4
	const bool use_core_push_descriptor = vk.apiVersion >= VK_API_VERSION_1_4;
#else
	const bool use_core_push_descriptor = false;
#endif
	MAKEDEVICEPROCADDR(vk, vkCmdPushDescriptorSetWithTemplateKHR);

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
	binding.descriptorCount = 2;
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

	VkDescriptorUpdateTemplateEntry template_entry{};
	template_entry.dstBinding = 0;
	template_entry.descriptorCount = 2;
	template_entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	template_entry.offset = offsetof(push_descriptor_template_data, descriptors) +
		offsetof(push_descriptor_template_element, descriptor);
	template_entry.stride = sizeof(push_descriptor_template_element);

	VkDescriptorUpdateTemplateCreateInfo template_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr };
	template_info.descriptorUpdateEntryCount = 1;
	template_info.pDescriptorUpdateEntries = &template_entry;
	template_info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS;
	template_info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
	template_info.pipelineLayout = layout;
	template_info.set = 0;

	VkDescriptorUpdateTemplate descriptor_template = VK_NULL_HANDLE;
	check(vkCreateDescriptorUpdateTemplate(vk.device, &template_info, nullptr, &descriptor_template));

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

	push_descriptor_template_data template_data{};
	template_data.prefix = 0x123456789abcdef0;
	template_data.descriptors[0].descriptor = dbi;
	template_data.descriptors[0].descriptor.range = 128;
	template_data.descriptors[0].padding = 0x1111111111111111;
	template_data.descriptors[1].descriptor = dbi;
	template_data.descriptors[1].descriptor.offset = 128;
	template_data.descriptors[1].descriptor.range = 128;
	template_data.descriptors[1].padding = 0x2222222222222222;
	template_data.suffix = 0x0fedcba987654321;
	if (use_core_push_descriptor)
	{
		vkCmdPushDescriptorSetWithTemplate(cmd, descriptor_template, layout, 0, &template_data);
	}
	else
	{
		pf_vkCmdPushDescriptorSetWithTemplateKHR(cmd, descriptor_template, layout, 0, &template_data);
	}

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
	vkDestroyDescriptorUpdateTemplate(vk.device, descriptor_template, nullptr);
	vkDestroyPipelineLayout(vk.device, layout, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, dsl, nullptr);
	vkDestroyBuffer(vk.device, buffer, nullptr);
	testFreeMemory(vk, memory);

	test_done(vk);
	return 0;
}
