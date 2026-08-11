// Test for vkCmdPushDescriptorSetWithTemplate2KHR

#include "vulkan_common.h"

#include <cstddef>
#include <vector>

struct push_descriptor_template2_data
{
	uint64_t prefix;
	VkDescriptorBufferInfo descriptor;
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
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.reqfeat14.pushDescriptor = VK_TRUE;
	reqs.device_extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_MAINTENANCE_6_EXTENSION_NAME);
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	auto vk = test_init(argc, argv, "vulkan_updatedescriptor_3", reqs);
	bench_start_iteration(vk.bench);

	std::vector<VkBuffer> buffers(1, VK_NULL_HANDLE);
	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = 256;
	buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	check(vkCreateBuffer(vk.device, &buffer_info, nullptr, &buffers[0]));

	std::vector<VkDeviceMemory> memory;
	testAllocateBufferMemory(vk, buffers, memory, false, false, false, "updatedescriptor_3_buffer");

	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	layout_info.bindingCount = 1;
	layout_info.pBindings = &binding;

	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	check(vkCreateDescriptorSetLayout(vk.device, &layout_info, nullptr, &set_layout));

	VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &set_layout;

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	check(vkCreatePipelineLayout(vk.device, &pipeline_layout_info, nullptr, &pipeline_layout));

	VkDescriptorUpdateTemplateEntry template_entry = {};
	template_entry.dstBinding = 0;
	template_entry.descriptorCount = 1;
	template_entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	template_entry.offset = offsetof(push_descriptor_template2_data, descriptor);
	template_entry.stride = sizeof(VkDescriptorBufferInfo);

	VkDescriptorUpdateTemplateCreateInfo template_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr
	};
	template_info.descriptorUpdateEntryCount = 1;
	template_info.pDescriptorUpdateEntries = &template_entry;
	template_info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS;
	template_info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
	template_info.pipelineLayout = pipeline_layout;
	template_info.set = 0;

	VkDescriptorUpdateTemplate descriptor_template = VK_NULL_HANDLE;
	check(vkCreateDescriptorUpdateTemplate(vk.device, &template_info, nullptr, &descriptor_template));
	MAKEDEVICEPROCADDR(vk, vkCmdPushDescriptorSetWithTemplate2KHR);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.queueFamilyIndex = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	check(vkCreateCommandPool(vk.device, &pool_info, nullptr, &command_pool));

	VkCommandBufferAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	allocate_info.commandPool = command_pool;
	allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	check(vkAllocateCommandBuffers(vk.device, &allocate_info, &command_buffer));

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(vkBeginCommandBuffer(command_buffer, &begin_info));

	push_descriptor_template2_data template_data = {};
	template_data.prefix = 0x123456789abcdef0;
	template_data.descriptor.buffer = buffers[0];
	template_data.descriptor.offset = 0;
	template_data.descriptor.range = VK_WHOLE_SIZE;
	template_data.suffix = 0x0fedcba987654321;

	VkPushDescriptorSetWithTemplateInfo command_info = {
		VK_STRUCTURE_TYPE_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_INFO, nullptr
	};
	command_info.descriptorUpdateTemplate = descriptor_template;
	command_info.layout = pipeline_layout;
	command_info.set = 0;
	command_info.pData = &template_data;
	pf_vkCmdPushDescriptorSetWithTemplate2KHR(command_buffer, &command_info);
#ifdef VULKAN_1_4
	if (vk.apiVersion >= VK_API_VERSION_1_4)
	{
		vkCmdPushDescriptorSetWithTemplate2(command_buffer, &command_info);
	}
#endif
	check(vkEndCommandBuffer(command_buffer));

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vk.device, 0, 0, &queue);
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fence = VK_NULL_HANDLE;
	check(vkCreateFence(vk.device, &fence_info, nullptr, &fence));
	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	check(vkQueueSubmit(queue, 1, &submit_info, fence));
	check(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));

	bench_stop_iteration(vk.bench);
	vkDestroyFence(vk.device, fence, nullptr);
	vkFreeCommandBuffers(vk.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vk.device, command_pool, nullptr);
	vkDestroyDescriptorUpdateTemplate(vk.device, descriptor_template, nullptr);
	vkDestroyPipelineLayout(vk.device, pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, set_layout, nullptr);
	vkDestroyBuffer(vk.device, buffers[0], nullptr);
	testFreeMemory(vk, memory[0]);
	test_done(vk);
	return 0;
}
