#include "vulkan_common.h"

#include <array>
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
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_MAINTENANCE_6_EXTENSION_NAME);
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vk = test_init(argc, argv, "vulkan_updatedescriptor_2", reqs);

	const VkDeviceSize buffer_size = 4096;
	const VkDeviceSize buffer_range = 256;
	constexpr uint32_t binding_count = 4;
	constexpr uint32_t buffer_count = binding_count * 2;

	std::vector<VkBuffer> buffers(buffer_count, VK_NULL_HANDLE);
	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = buffer_size;
	buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	for (uint32_t i = 0; i < buffer_count; i++)
	{
		check(vkCreateBuffer(vk.device, &buffer_info, nullptr, &buffers[i]));
	}

	std::vector<VkDeviceMemory> memory;
	testAllocateBufferMemory(vk, buffers, memory, false, false, false, "updatedescriptor_2_buffer");

	std::array<VkDescriptorSetLayoutBinding, binding_count> bindings = {};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[3].binding = 3;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	bindings[3].descriptorCount = 1;
	bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo dsl_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	dsl_info.bindingCount = binding_count;
	dsl_info.pBindings = bindings.data();

	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	check(vkCreateDescriptorSetLayout(vk.device, &dsl_info, nullptr, &set_layout));

	std::array<VkDescriptorPoolSize, 4> pool_sizes = {};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[0].descriptorCount = 2;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_sizes[1].descriptorCount = 2;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	pool_sizes[2].descriptorCount = 2;
	pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	pool_sizes[3].descriptorCount = 2;

	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 2;
	pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
	pool_info.pPoolSizes = pool_sizes.data();

	VkDescriptorPool pool = VK_NULL_HANDLE;
	check(vkCreateDescriptorPool(vk.device, &pool_info, nullptr, &pool));

	VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	alloc_info.descriptorPool = pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &set_layout;

	VkDescriptorSet src_set = VK_NULL_HANDLE;
	VkDescriptorSet dst_set = VK_NULL_HANDLE;
	check(vkAllocateDescriptorSets(vk.device, &alloc_info, &src_set));
	check(vkAllocateDescriptorSets(vk.device, &alloc_info, &dst_set));

	std::array<VkDescriptorBufferInfo, binding_count> src_infos = {};
	std::array<VkDescriptorBufferInfo, binding_count> dst_infos = {};
	for (uint32_t i = 0; i < binding_count; i++)
	{
		src_infos[i].buffer = buffers[i];
		src_infos[i].offset = 0;
		src_infos[i].range = buffer_range;

		dst_infos[i].buffer = buffers[i + binding_count];
		dst_infos[i].offset = 0;
		dst_infos[i].range = buffer_range;
	}

	std::array<VkWriteDescriptorSet, binding_count> src_writes = {};
	std::array<VkWriteDescriptorSet, binding_count> dst_writes = {};
	for (uint32_t i = 0; i < binding_count; i++)
	{
		src_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		src_writes[i].dstSet = src_set;
		src_writes[i].dstBinding = i;
		src_writes[i].descriptorCount = 1;
		src_writes[i].descriptorType = bindings[i].descriptorType;
		src_writes[i].pBufferInfo = &src_infos[i];

		dst_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		dst_writes[i].dstSet = dst_set;
		dst_writes[i].dstBinding = i;
		dst_writes[i].descriptorCount = 1;
		dst_writes[i].descriptorType = bindings[i].descriptorType;
		dst_writes[i].pBufferInfo = &dst_infos[i];
	}

	std::array<VkCopyDescriptorSet, binding_count> copies = {};
	for (uint32_t i = 0; i < binding_count; i++)
	{
		copies[i].sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
		copies[i].srcSet = src_set;
		copies[i].dstSet = dst_set;
		copies[i].srcBinding = i;
		copies[i].dstBinding = i;
		copies[i].descriptorCount = 1;
	}

	vkUpdateDescriptorSets(vk.device, binding_count, src_writes.data(), 0, nullptr);
	vkUpdateDescriptorSets(vk.device, binding_count, dst_writes.data(), 0, nullptr);
	vkUpdateDescriptorSets(vk.device, 0, nullptr, binding_count, copies.data());

	VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	layout_info.setLayoutCount = 1;
	layout_info.pSetLayouts = &set_layout;

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	check(vkCreatePipelineLayout(vk.device, &layout_info, nullptr, &pipeline_layout));

	VkDescriptorSetLayoutBinding push_binding = {};
	push_binding.binding = 0;
	push_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	push_binding.descriptorCount = 1;
	push_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo push_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	push_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	push_layout_info.bindingCount = 1;
	push_layout_info.pBindings = &push_binding;

	VkDescriptorSetLayout push_layout = VK_NULL_HANDLE;
	check(vkCreateDescriptorSetLayout(vk.device, &push_layout_info, nullptr, &push_layout));

	VkPipelineLayoutCreateInfo push_pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	push_pipeline_layout_info.setLayoutCount = 1;
	push_pipeline_layout_info.pSetLayouts = &push_layout;

	VkPipelineLayout push_pipeline_layout = VK_NULL_HANDLE;
	check(vkCreatePipelineLayout(vk.device, &push_pipeline_layout_info, nullptr, &push_pipeline_layout));

	MAKEDEVICEPROCADDR(vk, vkCmdBindDescriptorSets2KHR);
	MAKEDEVICEPROCADDR(vk, vkCmdPushDescriptorSetKHR);
	MAKEDEVICEPROCADDR(vk, vkCmdPushDescriptorSet2KHR);

	VkCommandPoolCreateInfo pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_create_info.queueFamilyIndex = 0;

	VkCommandPool cmdpool = VK_NULL_HANDLE;
	check(vkCreateCommandPool(vk.device, &pool_create_info, nullptr, &cmdpool));

	VkCommandBufferAllocateInfo cmd_alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	cmd_alloc_info.commandPool = cmdpool;
	cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	check(vkAllocateCommandBuffers(vk.device, &cmd_alloc_info, &cmd));

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(vkBeginCommandBuffer(cmd, &begin_info));

	VkDeviceSize ubo_alignment = vk.device_properties.limits.minUniformBufferOffsetAlignment;
	VkDeviceSize ssbo_alignment = vk.device_properties.limits.minStorageBufferOffsetAlignment;
	uint32_t dynamic_offsets[2] = {
		static_cast<uint32_t>((ubo_alignment > 0) ? ubo_alignment : 0),
		static_cast<uint32_t>((ssbo_alignment > 0) ? ssbo_alignment : 0)
	};
	if (static_cast<VkDeviceSize>(dynamic_offsets[0]) + buffer_range > buffer_size) dynamic_offsets[0] = 0;
	if (static_cast<VkDeviceSize>(dynamic_offsets[1]) + buffer_range > buffer_size) dynamic_offsets[1] = 0;

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &dst_set, 2, dynamic_offsets);

	VkBindDescriptorSetsInfo bind_info = { VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO, nullptr };
	bind_info.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	bind_info.layout = pipeline_layout;
	bind_info.firstSet = 0;
	bind_info.descriptorSetCount = 1;
	bind_info.pDescriptorSets = &dst_set;
	bind_info.dynamicOffsetCount = 2;
	bind_info.pDynamicOffsets = dynamic_offsets;

	pf_vkCmdBindDescriptorSets2KHR(cmd, &bind_info);

	VkDescriptorBufferInfo push_buffer_info = {};
	push_buffer_info.buffer = buffers[0];
	push_buffer_info.offset = 0;
	push_buffer_info.range = buffer_range;

	VkWriteDescriptorSet push_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	push_write.dstBinding = 0;
	push_write.descriptorCount = 1;
	push_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	push_write.pBufferInfo = &push_buffer_info;

	pf_vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, push_pipeline_layout, 0, 1, &push_write);

	VkPushDescriptorSetInfo push_info = { VK_STRUCTURE_TYPE_PUSH_DESCRIPTOR_SET_INFO, nullptr };
	push_info.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_info.layout = push_pipeline_layout;
	push_info.set = 0;
	push_info.descriptorWriteCount = 1;
	push_info.pDescriptorWrites = &push_write;

	pf_vkCmdPushDescriptorSet2KHR(cmd, &push_info);

	check(vkEndCommandBuffer(cmd));

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vk.device, 0, 0, &queue);

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	check(vkCreateFence(vk.device, &fence_info, nullptr, &fence));

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd;
	check(vkQueueSubmit(queue, 1, &submit_info, fence));
	check(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));

	vkDestroyFence(vk.device, fence, nullptr);
	vkFreeCommandBuffers(vk.device, cmdpool, 1, &cmd);
	vkDestroyCommandPool(vk.device, cmdpool, nullptr);

	vkDestroyPipelineLayout(vk.device, push_pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, push_layout, nullptr);
	vkDestroyPipelineLayout(vk.device, pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, set_layout, nullptr);
	vkDestroyDescriptorPool(vk.device, pool, nullptr);

	for (uint32_t i = 0; i < buffer_count; i++)
	{
		vkDestroyBuffer(vk.device, buffers[i], nullptr);
	}
	for (VkDeviceMemory mem : memory)
	{
		testFreeMemory(vk, mem);
	}

	test_done(vk);
	return 0;
}
