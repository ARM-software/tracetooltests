#include "vulkan_common.h"
#include <cstring>
#include <vector>
#include <assert.h>

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

static vulkan_req_t reqs;

struct buffer_with_memory
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceAddress address = 0;
	VkDeviceSize size = 0;
};

static buffer_with_memory create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage,
	VkMemoryPropertyFlags properties, bool device_address, VkBufferUsageFlags2 usage2 = 0)
{
	buffer_with_memory result_buffer{};
	result_buffer.size = size;

	VkBufferUsageFlags2CreateInfo usage2_info = { VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO, nullptr };
	usage2_info.usage = usage2;

	VkBufferCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	create_info.size = size;
	create_info.usage = usage;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (usage2 != 0)
	{
		create_info.pNext = &usage2_info;
	}

	VkResult vk_result = vkCreateBuffer(vulkan.device, &create_info, nullptr, &result_buffer.buffer);
	check(vk_result);

	VkMemoryRequirements mem_reqs{};
	vkGetBufferMemoryRequirements(vulkan.device, result_buffer.buffer, &mem_reqs);

	VkMemoryAllocateFlagsInfo flags_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };
	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	if (device_address)
	{
		flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		alloc_info.pNext = &flags_info;
	}
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(mem_reqs.memoryTypeBits, properties);
	vk_result = vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &result_buffer.memory);
	check(vk_result);

	vk_result = vkBindBufferMemory(vulkan.device, result_buffer.buffer, result_buffer.memory, 0);
	check(vk_result);

	if (device_address)
	{
		VkBufferDeviceAddressInfo address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
		address_info.buffer = result_buffer.buffer;
		result_buffer.address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
	}

	return result_buffer;
}

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
	VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT dgc_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT, nullptr };
	dgc_features.deviceGeneratedCommands = VK_TRUE;
	VkPhysicalDeviceMaintenance5FeaturesKHR maint5_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, nullptr };
	maint5_features.maintenance5 = VK_TRUE;
	dgc_features.pNext = &maint5_features;

	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.device_extensions.push_back("VK_EXT_device_generated_commands");
	reqs.device_extensions.push_back("VK_KHR_maintenance5");
	reqs.device_extensions.push_back("VK_KHR_depth_stencil_resolve");
	reqs.device_extensions.push_back("VK_KHR_dynamic_rendering");
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.bufferDeviceAddress = true;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&dgc_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_device_generated_commands_1", reqs);

	MAKEDEVICEPROCADDR(vulkan, vkGetGeneratedCommandsMemoryRequirementsEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdPreprocessGeneratedCommandsEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdExecuteGeneratedCommandsEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCreateIndirectCommandsLayoutEXT);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyIndirectCommandsLayoutEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCreateIndirectExecutionSetEXT);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyIndirectExecutionSetEXT);
	MAKEDEVICEPROCADDR(vulkan, vkUpdateIndirectExecutionSetPipelineEXT);

	VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT dgc_features_query = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT, nullptr };
	VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &dgc_features_query };
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &features2);

	VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT dgc_props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &dgc_props };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &props2);

	printf("Device generated commands features:\n");
	printf("\tdeviceGeneratedCommands = %s\n", dgc_features_query.deviceGeneratedCommands ? "true" : "false");
	printf("\tdynamicGeneratedPipelineLayout = %s\n", dgc_features_query.dynamicGeneratedPipelineLayout ? "true" : "false");

	printf("Device generated commands properties:\n");
	printf("\tmaxIndirectPipelineCount = %u\n", dgc_props.maxIndirectPipelineCount);
	printf("\tmaxIndirectShaderObjectCount = %u\n", dgc_props.maxIndirectShaderObjectCount);
	printf("\tmaxIndirectSequenceCount = %u\n", dgc_props.maxIndirectSequenceCount);
	printf("\tmaxIndirectCommandsTokenCount = %u\n", dgc_props.maxIndirectCommandsTokenCount);
	printf("\tmaxIndirectCommandsTokenOffset = %u\n", dgc_props.maxIndirectCommandsTokenOffset);
	printf("\tmaxIndirectCommandsIndirectStride = %u\n", dgc_props.maxIndirectCommandsIndirectStride);
	printf("\tsupportedIndirectCommandsInputModes = 0x%08x\n", (unsigned)dgc_props.supportedIndirectCommandsInputModes);
	printf("\tsupportedIndirectCommandsShaderStages = 0x%08x\n", (unsigned)dgc_props.supportedIndirectCommandsShaderStages);
	printf("\tsupportedIndirectCommandsShaderStagesPipelineBinding = 0x%08x\n", (unsigned)dgc_props.supportedIndirectCommandsShaderStagesPipelineBinding);
	printf("\tsupportedIndirectCommandsShaderStagesShaderBinding = 0x%08x\n", (unsigned)dgc_props.supportedIndirectCommandsShaderStagesShaderBinding);
	printf("\tdeviceGeneratedCommandsTransformFeedback = %s\n", dgc_props.deviceGeneratedCommandsTransformFeedback ? "true" : "false");
	printf("\tdeviceGeneratedCommandsMultiDrawIndirectCount = %s\n", dgc_props.deviceGeneratedCommandsMultiDrawIndirectCount ? "true" : "false");

	assert(dgc_props.maxIndirectSequenceCount >= 1);
	assert(dgc_props.maxIndirectPipelineCount >= 1);

	const uint32_t width = 1;
	const uint32_t height = 1;
	const uint32_t wg_size = 1;

	bench_start_iteration(vulkan.bench);

	buffer_with_memory storage_buffer = create_buffer(
		vulkan,
		sizeof(float) * 4,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		false);

	void* storage_data = nullptr;
	VkResult vk_result = vkMapMemory(vulkan.device, storage_buffer.memory, 0, storage_buffer.size, 0, &storage_data);
	check(vk_result);
	assert(storage_data != nullptr);
	memset(storage_data, 0, static_cast<size_t>(storage_buffer.size));
	vkUnmapMemory(vulkan.device, storage_buffer.memory);

	VkDescriptorSetLayoutBinding set_binding = {};
	set_binding.binding = 0;
	set_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	set_binding.descriptorCount = 1;
	set_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo set_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	set_layout_info.bindingCount = 1;
	set_layout_info.pBindings = &set_binding;
	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	vk_result = vkCreateDescriptorSetLayout(vulkan.device, &set_layout_info, nullptr, &set_layout);
	check(vk_result);

	VkDescriptorPoolSize pool_size = {};
	pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_size.descriptorCount = 1;
	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	vk_result = vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &descriptor_pool);
	check(vk_result);

	VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	alloc_info.descriptorPool = descriptor_pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &set_layout;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	vk_result = vkAllocateDescriptorSets(vulkan.device, &alloc_info, &descriptor_set);
	check(vk_result);

	VkDescriptorBufferInfo buffer_info = {};
	buffer_info.buffer = storage_buffer.buffer;
	buffer_info.offset = 0;
	buffer_info.range = storage_buffer.size;
	VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	write_set.dstSet = descriptor_set;
	write_set.dstBinding = 0;
	write_set.descriptorCount = 1;
	write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write_set.pBufferInfo = &buffer_info;
	vkUpdateDescriptorSets(vulkan.device, 1, &write_set, 0, nullptr);

	std::vector<uint32_t> code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);
	VkShaderModuleCreateInfo shader_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	shader_info.codeSize = code.size() * sizeof(uint32_t);
	shader_info.pCode = code.data();
	VkShaderModule shader_module = VK_NULL_HANDLE;
	vk_result = vkCreateShaderModule(vulkan.device, &shader_info, nullptr, &shader_module);
	check(vk_result);

	VkSpecializationMapEntry spec_entries[5] = {};
	for (uint32_t entry_index = 0; entry_index < 5; ++entry_index)
	{
		spec_entries[entry_index].constantID = entry_index;
		spec_entries[entry_index].offset = entry_index * 4;
		spec_entries[entry_index].size = 4;
	}

	int32_t spec_data[5] = { static_cast<int32_t>(wg_size), static_cast<int32_t>(wg_size), 1,
		static_cast<int32_t>(width), static_cast<int32_t>(height) };
	VkSpecializationInfo spec_info = {};
	spec_info.mapEntryCount = 5;
	spec_info.pMapEntries = spec_entries;
	spec_info.dataSize = sizeof(spec_data);
	spec_info.pData = spec_data;

	VkPipelineShaderStageCreateInfo stage_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage_info.module = shader_module;
	stage_info.pName = "main";
	stage_info.pSpecializationInfo = &spec_info;

	VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &set_layout;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	vk_result = vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &pipeline_layout);
	check(vk_result);

	VkPipelineCreateFlags2CreateInfo flags2_info = { VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO, nullptr };
	flags2_info.flags = VK_PIPELINE_CREATE_2_INDIRECT_BINDABLE_BIT_EXT;

	VkComputePipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	pipeline_info.stage = stage_info;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.pNext = &flags2_info;
	VkPipeline pipeline = VK_NULL_HANDLE;
	vk_result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(vk_result);

	VkIndirectExecutionSetPipelineInfoEXT exec_set_pipeline_info = { VK_STRUCTURE_TYPE_INDIRECT_EXECUTION_SET_PIPELINE_INFO_EXT, nullptr };
	exec_set_pipeline_info.initialPipeline = pipeline;
	exec_set_pipeline_info.maxPipelineCount = 1;
	VkIndirectExecutionSetCreateInfoEXT exec_set_info = { VK_STRUCTURE_TYPE_INDIRECT_EXECUTION_SET_CREATE_INFO_EXT, nullptr };
	exec_set_info.type = VK_INDIRECT_EXECUTION_SET_INFO_TYPE_PIPELINES_EXT;
	exec_set_info.info.pPipelineInfo = &exec_set_pipeline_info;
	VkIndirectExecutionSetEXT exec_set = VK_NULL_HANDLE;
	vk_result = pf_vkCreateIndirectExecutionSetEXT(vulkan.device, &exec_set_info, nullptr, &exec_set);
	check(vk_result);

	VkWriteIndirectExecutionSetPipelineEXT exec_set_write = { VK_STRUCTURE_TYPE_WRITE_INDIRECT_EXECUTION_SET_PIPELINE_EXT, nullptr };
	exec_set_write.index = 0;
	exec_set_write.pipeline = pipeline;
	pf_vkUpdateIndirectExecutionSetPipelineEXT(vulkan.device, exec_set, 1, &exec_set_write);

	VkIndirectCommandsLayoutTokenEXT token = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, nullptr };
	token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT;
	token.offset = 0;

	VkIndirectCommandsLayoutCreateInfoEXT layout_info = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_EXT, nullptr };
	layout_info.flags = VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EXPLICIT_PREPROCESS_BIT_EXT;
	layout_info.shaderStages = VK_SHADER_STAGE_COMPUTE_BIT;
	layout_info.indirectStride = sizeof(VkDispatchIndirectCommand);
	layout_info.pipelineLayout = pipeline_layout;
	layout_info.tokenCount = 1;
	layout_info.pTokens = &token;
	VkIndirectCommandsLayoutEXT indirect_layout = VK_NULL_HANDLE;
	vk_result = pf_vkCreateIndirectCommandsLayoutEXT(vulkan.device, &layout_info, nullptr, &indirect_layout);
	check(vk_result);

	buffer_with_memory indirect_commands = create_buffer(
		vulkan,
		sizeof(VkDispatchIndirectCommand),
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		true);

	VkDispatchIndirectCommand* indirect_cmd = nullptr;
	vk_result = vkMapMemory(vulkan.device, indirect_commands.memory, 0, indirect_commands.size, 0, reinterpret_cast<void**>(&indirect_cmd));
	check(vk_result);
	assert(indirect_cmd != nullptr);
	indirect_cmd->x = 1;
	indirect_cmd->y = 1;
	indirect_cmd->z = 1;
	vkUnmapMemory(vulkan.device, indirect_commands.memory);

	VkGeneratedCommandsPipelineInfoEXT generated_pipeline_info = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_PIPELINE_INFO_EXT, nullptr };
	generated_pipeline_info.pipeline = pipeline;

	VkGeneratedCommandsMemoryRequirementsInfoEXT mem_req_info = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_EXT, &generated_pipeline_info };
	mem_req_info.indirectExecutionSet = VK_NULL_HANDLE;
	mem_req_info.indirectCommandsLayout = indirect_layout;
	mem_req_info.maxSequenceCount = 1;
	mem_req_info.maxDrawCount = 1;
	VkMemoryRequirements2 preprocess_mem_reqs = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
	pf_vkGetGeneratedCommandsMemoryRequirementsEXT(vulkan.device, &mem_req_info, &preprocess_mem_reqs);

	assert(preprocess_mem_reqs.memoryRequirements.size > 0);

	buffer_with_memory preprocess_buffer = create_buffer(
		vulkan,
		preprocess_mem_reqs.memoryRequirements.size,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		0,
		true,
		VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT);

	assert(preprocess_buffer.size >= preprocess_mem_reqs.memoryRequirements.size);
	// Basic alignment check for 4 bytes, though often needs 256 or more depending on implementation
	assert(preprocess_buffer.address % 4 == 0);
	assert(indirect_commands.address % 4 == 0);

	VkGeneratedCommandsInfoEXT generated_info = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_EXT, &generated_pipeline_info };
	generated_info.shaderStages = VK_SHADER_STAGE_COMPUTE_BIT;
	generated_info.indirectExecutionSet = VK_NULL_HANDLE;
	generated_info.indirectCommandsLayout = indirect_layout;
	generated_info.indirectAddress = indirect_commands.address;
	generated_info.indirectAddressSize = indirect_commands.size;
	generated_info.preprocessAddress = preprocess_buffer.address;
	generated_info.preprocessSize = preprocess_buffer.size;
	generated_info.maxSequenceCount = 1;
	generated_info.sequenceCountAddress = 0;
	generated_info.maxDrawCount = 1;

	VkCommandPoolCreateInfo pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_create_info.queueFamilyIndex = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	vk_result = vkCreateCommandPool(vulkan.device, &pool_create_info, nullptr, &command_pool);
	check(vk_result);

	VkCommandBufferAllocateInfo cmd_alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	cmd_alloc_info.commandPool = command_pool;
	cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc_info.commandBufferCount = 2;
	VkCommandBuffer command_buffers[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	vk_result = vkAllocateCommandBuffers(vulkan.device, &cmd_alloc_info, command_buffers);
	check(vk_result);

	VkCommandBuffer state_cmd = command_buffers[0];
	VkCommandBuffer main_cmd = command_buffers[1];

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vk_result = vkBeginCommandBuffer(state_cmd, &begin_info);
	check(vk_result);
	vkCmdBindPipeline(state_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdBindDescriptorSets(state_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

	vk_result = vkBeginCommandBuffer(main_cmd, &begin_info);
	check(vk_result);
	pf_vkCmdPreprocessGeneratedCommandsEXT(main_cmd, &generated_info, state_cmd);
	VkMemoryBarrier preprocess_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
	preprocess_barrier.srcAccessMask = VK_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_EXT;
	preprocess_barrier.dstAccessMask = VK_ACCESS_COMMAND_PREPROCESS_READ_BIT_EXT;
	vkCmdPipelineBarrier(main_cmd,
		VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_EXT,
		VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_EXT,
		0,
		1,
		&preprocess_barrier,
		0,
		nullptr,
		0,
		nullptr);
	vkCmdBindPipeline(main_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdBindDescriptorSets(main_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
	pf_vkCmdExecuteGeneratedCommandsEXT(main_cmd, VK_TRUE, &generated_info);
	vk_result = vkEndCommandBuffer(main_cmd);
	check(vk_result);
	vk_result = vkEndCommandBuffer(state_cmd);
	check(vk_result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &main_cmd;
	vk_result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(vk_result);
	vk_result = vkQueueWaitIdle(queue);
	check(vk_result);

	bench_stop_iteration(vulkan.bench);

	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	pf_vkDestroyIndirectCommandsLayoutEXT(vulkan.device, indirect_layout, nullptr);
	pf_vkDestroyIndirectExecutionSetEXT(vulkan.device, exec_set, nullptr);
	vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyShaderModule(vulkan.device, shader_module, nullptr);
	vkDestroyDescriptorPool(vulkan.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, set_layout, nullptr);
	vkDestroyBuffer(vulkan.device, storage_buffer.buffer, nullptr);
	testFreeMemory(vulkan, storage_buffer.memory);
	vkDestroyBuffer(vulkan.device, indirect_commands.buffer, nullptr);
	testFreeMemory(vulkan, indirect_commands.memory);
	vkDestroyBuffer(vulkan.device, preprocess_buffer.buffer, nullptr);
	testFreeMemory(vulkan, preprocess_buffer.memory);

	test_done(vulkan);
	return 0;
}
