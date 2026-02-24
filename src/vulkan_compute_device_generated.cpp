#include "vulkan_common.h"
#include "vulkan_compute_common.h"
#include <assert.h>
#include <cstring>
#include <vector>

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

struct buffer_with_memory
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceAddress address = 0;
	VkDeviceSize size = 0;
};

static buffer_with_memory create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage,
	VkMemoryPropertyFlags properties, bool device_address, VkBufferUsageFlags2 usage2 = 0, uint32_t required_mem_bits = 0)
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

	VkResult result = vkCreateBuffer(vulkan.device, &create_info, nullptr, &result_buffer.buffer);
	check(result);

	VkMemoryRequirements mem_reqs{};
	vkGetBufferMemoryRequirements(vulkan.device, result_buffer.buffer, &mem_reqs);

	uint32_t allowed_mem_bits = mem_reqs.memoryTypeBits;
	if (required_mem_bits != 0)
	{
		allowed_mem_bits &= required_mem_bits;
		assert(allowed_mem_bits != 0);
	}

	VkMemoryAllocateFlagsInfo flags_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };
	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	if (device_address)
	{
		flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		alloc_info.pNext = &flags_info;
	}
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(allowed_mem_bits, properties);
	result = vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &result_buffer.memory);
	check(result);

	result = vkBindBufferMemory(vulkan.device, result_buffer.buffer, result_buffer.memory, 0);
	check(result);

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
	compute_usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return compute_cmdopt(i, argc, argv, reqs);
}

static void create_compute_pipeline_dgc(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
{
	VkShaderModuleCreateInfo create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	create_info.pCode = r.code.data();
	create_info.codeSize = r.code.size() * sizeof(uint32_t);
	VkResult result = vkCreateShaderModule(vulkan.device, &create_info, nullptr, &r.computeShaderModule);
	check(result);

	std::vector<VkSpecializationMapEntry> spec_entries(5);
	for (uint32_t entry_index = 0; entry_index < spec_entries.size(); ++entry_index)
	{
		spec_entries[entry_index].constantID = entry_index;
		spec_entries[entry_index].offset = entry_index * 4;
		spec_entries[entry_index].size = 4;
	}

	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int wg_size = std::get<int>(reqs.options.at("wg_size"));
	std::vector<int32_t> spec_data(5);
	spec_data[0] = wg_size;
	spec_data[1] = wg_size;
	spec_data[2] = 1;
	spec_data[3] = width;
	spec_data[4] = height;

	VkSpecializationInfo spec_info = {};
	spec_info.mapEntryCount = spec_entries.size();
	spec_info.pMapEntries = spec_entries.data();
	spec_info.dataSize = spec_data.size() * sizeof(int32_t);
	spec_info.pData = spec_data.data();

	VkPipelineShaderStageCreateInfo stage_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage_info.module = r.computeShaderModule;
	stage_info.pName = "main";
	stage_info.pSpecializationInfo = &spec_info;

	VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	layout_info.setLayoutCount = 1;
	layout_info.pSetLayouts = &r.descriptorSetLayout;
	result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &r.pipelineLayout);
	check(result);

	VkPipelineCreateFlags2CreateInfo flags2_info = { VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO, nullptr };
	flags2_info.flags = VK_PIPELINE_CREATE_2_INDIRECT_BINDABLE_BIT_EXT;

	VkPipelineCreationFeedback creationfeedback = { 0, 0 };
	VkPipelineCreationFeedbackCreateInfo feedinfo = { VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO, nullptr, &creationfeedback, 0, nullptr };
	if (reqs.apiVersion == VK_API_VERSION_1_3)
	{
		flags2_info.pNext = &feedinfo;
	}

	VkComputePipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	pipeline_info.stage = stage_info;
	pipeline_info.layout = r.pipelineLayout;
	pipeline_info.pNext = &flags2_info;

	if (reqs.options.count("pipelinecache"))
	{
		char* blob = nullptr;
		VkPipelineCacheCreateInfo cache_info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr };
		cache_info.flags = 0;
		if (reqs.options.count("cachefile") && exists_blob(std::get<std::string>(reqs.options.at("cachefile"))))
		{
			ILOG("Reading pipeline cache data from %s", std::get<std::string>(reqs.options.at("cachefile")).c_str());
			uint32_t size = 0;
			blob = load_blob(std::get<std::string>(reqs.options.at("cachefile")), &size);
			cache_info.initialDataSize = size;
			cache_info.pInitialData = blob;
		}
		result = vkCreatePipelineCache(vulkan.device, &cache_info, nullptr, &r.cache);
		check(result);
		free(blob);
	}

	result = vkCreateComputePipelines(vulkan.device, r.cache, 1, &pipeline_info, nullptr, &r.pipeline);
	if (reqs.options.count("allow_compile_required") && (result == VK_PIPELINE_COMPILE_REQUIRED || result == VK_PIPELINE_COMPILE_REQUIRED_EXT))
	{
		printf("Pipeline compile required, retrying without FAIL_ON flag\n");
		pipeline_info.flags &= ~VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
		result = vkCreateComputePipelines(vulkan.device, r.cache, 1, &pipeline_info, nullptr, &r.pipeline);
	}
	if (reqs.options.count("allow_compile_required") && (result == VK_PIPELINE_COMPILE_REQUIRED || result == VK_PIPELINE_COMPILE_REQUIRED_EXT))
	{
		printf("Pipeline still needs compilation, skipping pipeline creation\n");
		r.pipeline = VK_NULL_HANDLE;
		return;
	}
	check(result);

	if (reqs.apiVersion == VK_API_VERSION_1_3)
	{
		if (creationfeedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT)
		{
			ILOG("VkPipelineCreationFeedback value = %lu ns", (unsigned long)creationfeedback.duration);
		}
		else
		{
			ILOG("VkPipelineCreationFeedback invalid");
		}
	}
}

int main(int argc, char** argv)
{
	p__loops = 1;

	VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT dgc_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT, nullptr };
	dgc_features.deviceGeneratedCommands = VK_TRUE;
	VkPhysicalDeviceMaintenance5FeaturesKHR maint5_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, nullptr };
	maint5_features.maintenance5 = VK_TRUE;
	dgc_features.pNext = &maint5_features;

	vulkan_req_t req;
	req.options["width"] = 640;
	req.options["height"] = 480;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.device_extensions.push_back("VK_EXT_device_generated_commands");
	req.device_extensions.push_back("VK_KHR_maintenance5");
	req.apiVersion = VK_API_VERSION_1_3;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.bufferDeviceAddress = true;
	req.extension_features = reinterpret_cast<VkBaseInStructure*>(&dgc_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_device_generated", req);

	MAKEDEVICEPROCADDR(vulkan, vkGetGeneratedCommandsMemoryRequirementsEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdPreprocessGeneratedCommandsEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdExecuteGeneratedCommandsEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCreateIndirectCommandsLayoutEXT);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyIndirectCommandsLayoutEXT);

	VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT dgc_features_query = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT, nullptr };
	VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &dgc_features_query };
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &features2);
	if (!dgc_features_query.deviceGeneratedCommands)
	{
		printf("Device generated commands not supported\n");
		exit(77);
	}

	VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT dgc_props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &dgc_props };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &props2);

	assert(dgc_props.maxIndirectSequenceCount >= 1);
	assert(dgc_props.maxIndirectCommandsTokenCount >= 1);
	assert(dgc_props.maxIndirectCommandsIndirectStride >= sizeof(VkDispatchIndirectCommand));
	assert(dgc_props.supportedIndirectCommandsShaderStages & VK_SHADER_STAGE_COMPUTE_BIT);

	compute_resources r = compute_init(vulkan, req);
	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	const int workgroup_size = std::get<int>(req.options.at("wg_size"));
	assert(workgroup_size > 0);

	VkResult result;
	VkDescriptorSetLayoutBinding descriptor_binding = {};
	descriptor_binding.binding = 0;
	descriptor_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptor_binding.descriptorCount = 1;
	descriptor_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo descriptor_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	descriptor_layout_info.bindingCount = 1;
	descriptor_layout_info.pBindings = &descriptor_binding;
	result = vkCreateDescriptorSetLayout(vulkan.device, &descriptor_layout_info, nullptr, &r.descriptorSetLayout);
	check(result);

	VkDescriptorPoolSize pool_size = {};
	pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_size.descriptorCount = 1;
	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;
	result = vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &r.descriptorPool);
	check(result);

	VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	alloc_info.descriptorPool = r.descriptorPool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &r.descriptorSetLayout;
	result = vkAllocateDescriptorSets(vulkan.device, &alloc_info, &r.descriptorSet);
	check(result);

	VkDescriptorBufferInfo buffer_info = {};
	buffer_info.buffer = r.buffer;
	buffer_info.offset = 0;
	buffer_info.range = r.buffer_size;
	VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	write_set.dstSet = r.descriptorSet;
	write_set.dstBinding = 0;
	write_set.descriptorCount = 1;
	write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write_set.pBufferInfo = &buffer_info;
	vkUpdateDescriptorSets(vulkan.device, 1, &write_set, 0, nullptr);

	r.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);
	create_compute_pipeline_dgc(vulkan, r, req);

	VkIndirectCommandsLayoutTokenEXT token = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, nullptr };
	token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT;
	token.offset = 0;

	VkIndirectCommandsLayoutCreateInfoEXT layout_info = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_EXT, nullptr };
	layout_info.flags = VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EXPLICIT_PREPROCESS_BIT_EXT;
	layout_info.shaderStages = VK_SHADER_STAGE_COMPUTE_BIT;
	layout_info.indirectStride = sizeof(VkDispatchIndirectCommand);
	layout_info.pipelineLayout = r.pipelineLayout;
	layout_info.tokenCount = 1;
	layout_info.pTokens = &token;
	VkIndirectCommandsLayoutEXT indirect_layout = VK_NULL_HANDLE;
	result = pf_vkCreateIndirectCommandsLayoutEXT(vulkan.device, &layout_info, nullptr, &indirect_layout);
	check(result);

	buffer_with_memory indirect_commands = create_buffer(
		vulkan,
		sizeof(VkDispatchIndirectCommand),
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		true);

	VkDispatchIndirectCommand* indirect_cmd = nullptr;
	result = vkMapMemory(vulkan.device, indirect_commands.memory, 0, indirect_commands.size, 0, reinterpret_cast<void**>(&indirect_cmd));
	check(result);
	assert(indirect_cmd != nullptr);
	const uint32_t group_x = (static_cast<uint32_t>(width) + static_cast<uint32_t>(workgroup_size) - 1) / static_cast<uint32_t>(workgroup_size);
	const uint32_t group_y = (static_cast<uint32_t>(height) + static_cast<uint32_t>(workgroup_size) - 1) / static_cast<uint32_t>(workgroup_size);
	indirect_cmd->x = group_x;
	indirect_cmd->y = group_y;
	indirect_cmd->z = 1;
	vkUnmapMemory(vulkan.device, indirect_commands.memory);

	VkGeneratedCommandsPipelineInfoEXT generated_pipeline_info = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_PIPELINE_INFO_EXT, nullptr };
	generated_pipeline_info.pipeline = r.pipeline;

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
		VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
		preprocess_mem_reqs.memoryRequirements.memoryTypeBits);

	assert(preprocess_buffer.size >= preprocess_mem_reqs.memoryRequirements.size);
	assert(preprocess_buffer.address != 0);
	assert(indirect_commands.address != 0);
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

	VkCommandBufferAllocateInfo state_alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	state_alloc_info.commandPool = r.commandPool;
	state_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	state_alloc_info.commandBufferCount = 1;
	VkCommandBuffer state_cmd = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &state_alloc_info, &state_cmd);
	check(result);

	for (unsigned frame = 0; frame < p__loops; ++frame)
	{
		test_marker(vulkan, "Frame " + std::to_string(frame));
		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		result = vkBeginCommandBuffer(state_cmd, &begin_info);
		check(result);
		vkCmdBindPipeline(state_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
		vkCmdBindDescriptorSets(state_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, nullptr);

		result = vkBeginCommandBuffer(r.commandBuffer, &begin_info);
		check(result);
		pf_vkCmdPreprocessGeneratedCommandsEXT(r.commandBuffer, &generated_info, state_cmd);
		VkMemoryBarrier preprocess_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
		preprocess_barrier.srcAccessMask = VK_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_EXT;
		preprocess_barrier.dstAccessMask = VK_ACCESS_COMMAND_PREPROCESS_READ_BIT_EXT;
		vkCmdPipelineBarrier(r.commandBuffer,
			VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_EXT,
			VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_EXT,
			0,
			1,
			&preprocess_barrier,
			0,
			nullptr,
			0,
			nullptr);
		vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
		vkCmdBindDescriptorSets(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, nullptr);
		pf_vkCmdExecuteGeneratedCommandsEXT(r.commandBuffer, VK_TRUE, &generated_info);
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);
		result = vkEndCommandBuffer(state_cmd);
		check(result);

		compute_submit(vulkan, r, req);
		vkResetCommandBuffer(state_cmd, 0);
	}

	pf_vkDestroyIndirectCommandsLayoutEXT(vulkan.device, indirect_layout, nullptr);
	vkDestroyBuffer(vulkan.device, indirect_commands.buffer, nullptr);
	testFreeMemory(vulkan, indirect_commands.memory);
	vkDestroyBuffer(vulkan.device, preprocess_buffer.buffer, nullptr);
	testFreeMemory(vulkan, preprocess_buffer.memory);

	compute_done(vulkan, r, req);
	test_done(vulkan);
	return 0;
}
