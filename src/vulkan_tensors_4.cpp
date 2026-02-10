// Minimal data graph constants test based on Vulkan-Samples graph_constants.
// Shader generated with:
//   xxd -i -n vulkan_tensors_4_conv2d_spv content/vulkan-samples/shaders/tensor_and_data_graph/spirv/conv2d.spvasm.spv > src/vulkan_tensors_4_conv2d.inc

#include "vulkan_common.h"

#include <array>
#include <vector>

#include "vulkan_tensors_4_conv2d.inc"

struct tensor_resource
{
	VkTensorARM tensor = VK_NULL_HANDLE;
	VkTensorViewARM view = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
	std::array<int64_t, 4> dimensions = {};
	VkTensorDescriptionARM description = { VK_STRUCTURE_TYPE_TENSOR_DESCRIPTION_ARM, nullptr };
};

struct tensor_functions
{
	PFN_vkCreateTensorARM create = nullptr;
	PFN_vkDestroyTensorARM destroy = nullptr;
	PFN_vkGetTensorMemoryRequirementsARM get_requirements = nullptr;
	PFN_vkBindTensorMemoryARM bind_memory = nullptr;
	PFN_vkCreateTensorViewARM create_view = nullptr;
	PFN_vkDestroyTensorViewARM destroy_view = nullptr;
};

struct data_graph_functions
{
	PFN_vkCreateDataGraphPipelinesARM create_pipelines = nullptr;
	PFN_vkCreateDataGraphPipelineSessionARM create_session = nullptr;
	PFN_vkGetDataGraphPipelineSessionBindPointRequirementsARM get_bind_point_requirements = nullptr;
	PFN_vkGetDataGraphPipelineSessionMemoryRequirementsARM get_memory_requirements = nullptr;
	PFN_vkBindDataGraphPipelineSessionMemoryARM bind_session_memory = nullptr;
	PFN_vkDestroyDataGraphPipelineSessionARM destroy_session = nullptr;
	PFN_vkCmdDispatchDataGraphARM cmd_dispatch = nullptr;
};

struct data_graph_session
{
	VkDataGraphPipelineSessionARM session = VK_NULL_HANDLE;
	std::vector<VkDeviceMemory> memories;
};

static VkShaderModule create_shader_module(const vulkan_setup_t& vulkan, const unsigned char* spirv, uint32_t spirv_len)
{
	const std::vector<uint32_t> code = copy_shader(const_cast<unsigned char*>(spirv), spirv_len);
	VkShaderModuleCreateInfo create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	create_info.codeSize = code.size() * sizeof(uint32_t);
	create_info.pCode = code.data();
	VkShaderModule module = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &create_info, nullptr, &module);
	check(result);
	return module;
}

static tensor_resource create_tensor(const vulkan_setup_t& vulkan, const tensor_functions& f,
                                     const std::array<int64_t, 4>& dimensions, VkTensorUsageFlagsARM usage,
                                     VkTensorTilingARM tiling, VkFormat format, VkMemoryPropertyFlags memory_properties)
{
	tensor_resource res;
	res.dimensions = dimensions;
	res.description.tiling = tiling;
	res.description.format = format;
	res.description.dimensionCount = static_cast<uint32_t>(res.dimensions.size());
	res.description.pDimensions = res.dimensions.data();
	res.description.pStrides = nullptr;
	res.description.usage = usage;

	VkTensorCreateInfoARM create_info = { VK_STRUCTURE_TYPE_TENSOR_CREATE_INFO_ARM, nullptr };
	create_info.flags = 0;
	create_info.pDescription = &res.description;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_info.queueFamilyIndexCount = 0;
	create_info.pQueueFamilyIndices = nullptr;

	VkResult result = f.create(vulkan.device, &create_info, nullptr, &res.tensor);
	check(result);

	VkTensorMemoryRequirementsInfoARM req_info = { VK_STRUCTURE_TYPE_TENSOR_MEMORY_REQUIREMENTS_INFO_ARM, nullptr };
	req_info.tensor = res.tensor;
	VkMemoryRequirements2 memreq = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
	f.get_requirements(vulkan.device, &req_info, &memreq);
	assert(memreq.memoryRequirements.size > 0);

	const VkDeviceSize alignment = memreq.memoryRequirements.alignment;
	const VkDeviceSize align_mod = memreq.memoryRequirements.size % alignment;
	res.size = (align_mod == 0) ? memreq.memoryRequirements.size : (memreq.memoryRequirements.size + alignment - align_mod);
	const uint32_t memory_type_index = get_device_memory_type(memreq.memoryRequirements.memoryTypeBits, memory_properties);

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	alloc_info.allocationSize = res.size;
	alloc_info.memoryTypeIndex = memory_type_index;
	result = vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &res.memory);
	check(result);

	VkBindTensorMemoryInfoARM bind_info = { VK_STRUCTURE_TYPE_BIND_TENSOR_MEMORY_INFO_ARM, nullptr };
	bind_info.tensor = res.tensor;
	bind_info.memory = res.memory;
	bind_info.memoryOffset = 0;
	result = f.bind_memory(vulkan.device, 1, &bind_info);
	check(result);

	VkTensorViewCreateInfoARM view_info = { VK_STRUCTURE_TYPE_TENSOR_VIEW_CREATE_INFO_ARM, nullptr };
	view_info.flags = 0;
	view_info.tensor = res.tensor;
	view_info.format = format;
	result = f.create_view(vulkan.device, &view_info, nullptr, &res.view);
	check(result);

	return res;
}

static void destroy_tensor(const vulkan_setup_t& vulkan, const tensor_functions& f, tensor_resource& res)
{
	if (res.view != VK_NULL_HANDLE)
	{
		f.destroy_view(vulkan.device, res.view, nullptr);
		res.view = VK_NULL_HANDLE;
	}
	if (res.tensor != VK_NULL_HANDLE)
	{
		f.destroy(vulkan.device, res.tensor, nullptr);
		res.tensor = VK_NULL_HANDLE;
	}
	if (res.memory != VK_NULL_HANDLE)
	{
		testFreeMemory(vulkan, res.memory);
		res.memory = VK_NULL_HANDLE;
	}
}

static data_graph_session create_data_graph_session(const vulkan_setup_t& vulkan, const data_graph_functions& f, VkPipeline pipeline)
{
	data_graph_session session;
	VkDataGraphPipelineSessionCreateInfoARM create_info = { VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SESSION_CREATE_INFO_ARM, nullptr };
	create_info.flags = 0;
	create_info.dataGraphPipeline = pipeline;
	VkResult result = f.create_session(vulkan.device, &create_info, nullptr, &session.session);
	check(result);

	VkDataGraphPipelineSessionBindPointRequirementsInfoARM bind_req_info = {
	    VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_REQUIREMENTS_INFO_ARM, nullptr };
	bind_req_info.session = session.session;
	uint32_t requirement_count = 0;
	result = f.get_bind_point_requirements(vulkan.device, &bind_req_info, &requirement_count, nullptr);
	check(result);
	if (requirement_count == 0)
	{
		return session;
	}

	std::vector<VkDataGraphPipelineSessionBindPointRequirementARM> requirements(requirement_count);
	for (uint32_t i = 0; i < requirement_count; ++i)
	{
		requirements[i] = { VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_REQUIREMENT_ARM, nullptr };
	}
	result = f.get_bind_point_requirements(vulkan.device, &bind_req_info, &requirement_count, requirements.data());
	check(result);

	for (const auto& requirement : requirements)
	{
		assert(requirement.numObjects > 0);
		assert(requirement.bindPointType == VK_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_TYPE_MEMORY_ARM);

		for (uint32_t object_index = 0; object_index < requirement.numObjects; ++object_index)
		{
			VkMemoryRequirements2 memreq = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
			VkDataGraphPipelineSessionMemoryRequirementsInfoARM mem_info = {
			    VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SESSION_MEMORY_REQUIREMENTS_INFO_ARM, nullptr };
			mem_info.session = session.session;
			mem_info.bindPoint = requirement.bindPoint;
			mem_info.objectIndex = object_index;
			f.get_memory_requirements(vulkan.device, &mem_info, &memreq);
			assert(memreq.memoryRequirements.size > 0);

			VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
			alloc_info.allocationSize = memreq.memoryRequirements.size;
			alloc_info.memoryTypeIndex = get_device_memory_type(memreq.memoryRequirements.memoryTypeBits, 0);
			VkDeviceMemory memory = VK_NULL_HANDLE;
			result = vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &memory);
			check(result);

			VkBindDataGraphPipelineSessionMemoryInfoARM bind_info = {
			    VK_STRUCTURE_TYPE_BIND_DATA_GRAPH_PIPELINE_SESSION_MEMORY_INFO_ARM, nullptr };
			bind_info.session = session.session;
			bind_info.bindPoint = requirement.bindPoint;
			bind_info.objectIndex = object_index;
			bind_info.memory = memory;
			bind_info.memoryOffset = 0;
			result = f.bind_session_memory(vulkan.device, 1, &bind_info);
			check(result);

			session.memories.push_back(memory);
		}
	}

	return session;
}

static void destroy_data_graph_session(const vulkan_setup_t& vulkan, const data_graph_functions& f, data_graph_session& session)
{
	if (session.session != VK_NULL_HANDLE)
	{
		f.destroy_session(vulkan.device, session.session, nullptr);
		session.session = VK_NULL_HANDLE;
	}
	for (VkDeviceMemory memory : session.memories)
	{
		if (memory != VK_NULL_HANDLE)
		{
			testFreeMemory(vulkan, memory);
		}
	}
	session.memories.clear();
}

static void fill_weights(std::vector<float>& weights)
{
	const int channels = 3;
	const int kernel = 3;
	weights.assign(channels * kernel * kernel * channels, 0.0f);

	for (int i = 0; i < channels; ++i)
	{
		const int oc = i;
		const int ic = i;
		weights[(((oc * kernel) + 0) * kernel + 1) * channels + ic] = -0.5f;
		weights[(((oc * kernel) + 1) * kernel + 0) * channels + ic] = -0.5f;
		weights[(((oc * kernel) + 1) * kernel + 1) * channels + ic] = 2.0f;
		weights[(((oc * kernel) + 1) * kernel + 2) * channels + ic] = -0.5f;
		weights[(((oc * kernel) + 2) * kernel + 1) * channels + ic] = -0.5f;
	}
}

int main(int argc, char** argv)
{
	VkPhysicalDeviceTensorFeaturesARM tensor_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM, nullptr };
	tensor_features.tensors = VK_TRUE;
	tensor_features.shaderTensorAccess = VK_TRUE;

	VkPhysicalDeviceDataGraphFeaturesARM data_graph_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DATA_GRAPH_FEATURES_ARM, &tensor_features };
	data_graph_features.dataGraph = VK_TRUE;
	data_graph_features.dataGraphShaderModule = VK_TRUE;

	vulkan_req_t reqs;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&data_graph_features);

	reqs.device_extensions.push_back("VK_ARM_tensors");
	reqs.device_extensions.push_back("VK_ARM_data_graph");
	reqs.device_extensions.push_back("VK_KHR_maintenance5");
	reqs.device_extensions.push_back("VK_KHR_deferred_host_operations");
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.apiVersion = VK_API_VERSION_1_3;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_tensors_4", reqs);

	bench_start_iteration(vulkan.bench);

	MAKEDEVICEPROCADDR(vulkan, vkCreateTensorARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyTensorARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetTensorMemoryRequirementsARM);
	MAKEDEVICEPROCADDR(vulkan, vkCreateTensorViewARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyTensorViewARM);
	MAKEDEVICEPROCADDR(vulkan, vkBindTensorMemoryARM);

	MAKEDEVICEPROCADDR(vulkan, vkCreateDataGraphPipelinesARM);
	MAKEDEVICEPROCADDR(vulkan, vkCreateDataGraphPipelineSessionARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetDataGraphPipelineSessionBindPointRequirementsARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetDataGraphPipelineSessionMemoryRequirementsARM);
	MAKEDEVICEPROCADDR(vulkan, vkBindDataGraphPipelineSessionMemoryARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyDataGraphPipelineSessionARM);
	MAKEDEVICEPROCADDR(vulkan, vkCmdDispatchDataGraphARM);

	tensor_functions tensor_funcs = {
	    pf_vkCreateTensorARM,
	    pf_vkDestroyTensorARM,
	    pf_vkGetTensorMemoryRequirementsARM,
	    pf_vkBindTensorMemoryARM,
	    pf_vkCreateTensorViewARM,
	    pf_vkDestroyTensorViewARM,
	};

	data_graph_functions graph_funcs = {
	    pf_vkCreateDataGraphPipelinesARM,
	    pf_vkCreateDataGraphPipelineSessionARM,
	    pf_vkGetDataGraphPipelineSessionBindPointRequirementsARM,
	    pf_vkGetDataGraphPipelineSessionMemoryRequirementsARM,
	    pf_vkBindDataGraphPipelineSessionMemoryARM,
	    pf_vkDestroyDataGraphPipelineSessionARM,
	    pf_vkCmdDispatchDataGraphARM,
	};

	const std::array<int64_t, 4> io_dims = { 1, 20, 20, 3 };

	tensor_resource input_tensor = create_tensor(
	    vulkan, tensor_funcs, io_dims, VK_TENSOR_USAGE_DATA_GRAPH_BIT_ARM,
	    VK_TENSOR_TILING_OPTIMAL_ARM, VK_FORMAT_R32_SFLOAT, 0);
	tensor_resource output_tensor = create_tensor(
	    vulkan, tensor_funcs, io_dims, VK_TENSOR_USAGE_DATA_GRAPH_BIT_ARM,
	    VK_TENSOR_TILING_OPTIMAL_ARM, VK_FORMAT_R32_SFLOAT, 0);

	std::array<VkDescriptorSetLayoutBinding, 2> bindings = {};
	for (size_t i = 0; i < bindings.size(); ++i)
	{
		bindings[i].binding = static_cast<uint32_t>(i);
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
	}

	VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
	layout_info.pBindings = bindings.data();
	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	VkResult result = vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &set_layout);
	check(result);

	VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &set_layout;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &pipeline_layout);
	check(result);

	VkDescriptorPoolSize pool_size = {};
	pool_size.type = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
	pool_size.descriptorCount = 2;
	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	result = vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &descriptor_pool);
	check(result);

	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	alloc_info.descriptorPool = descriptor_pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &set_layout;
	result = vkAllocateDescriptorSets(vulkan.device, &alloc_info, &descriptor_set);
	check(result);

	VkWriteDescriptorSetTensorARM tensor_infos[2] = {
	    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_TENSOR_ARM, nullptr, 1, &input_tensor.view },
	    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_TENSOR_ARM, nullptr, 1, &output_tensor.view },
	};
	VkWriteDescriptorSet writes[2] = {
	    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &tensor_infos[0] },
	    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &tensor_infos[1] },
	};
	writes[0].dstSet = descriptor_set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
	writes[1].dstSet = descriptor_set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
	vkUpdateDescriptorSets(vulkan.device, 2, writes, 0, nullptr);

	std::array<int64_t, 4> weights_dims = { 3, 3, 3, 3 };
	std::vector<float> weights_data;
	fill_weights(weights_data);
	VkTensorDescriptionARM weights_desc = { VK_STRUCTURE_TYPE_TENSOR_DESCRIPTION_ARM, nullptr };
	weights_desc.tiling = VK_TENSOR_TILING_LINEAR_ARM;
	weights_desc.format = VK_FORMAT_R32_SFLOAT;
	weights_desc.dimensionCount = static_cast<uint32_t>(weights_dims.size());
	weights_desc.pDimensions = weights_dims.data();
	weights_desc.pStrides = nullptr;
	weights_desc.usage = VK_TENSOR_USAGE_DATA_GRAPH_BIT_ARM;

	std::array<int64_t, 1> bias_dims = { 3 };
	std::array<float, 3> bias_data = { 0.0f, 0.0f, 0.0f };
	VkTensorDescriptionARM bias_desc = { VK_STRUCTURE_TYPE_TENSOR_DESCRIPTION_ARM, nullptr };
	bias_desc.tiling = VK_TENSOR_TILING_LINEAR_ARM;
	bias_desc.format = VK_FORMAT_R32_SFLOAT;
	bias_desc.dimensionCount = static_cast<uint32_t>(bias_dims.size());
	bias_desc.pDimensions = bias_dims.data();
	bias_desc.pStrides = nullptr;
	bias_desc.usage = VK_TENSOR_USAGE_DATA_GRAPH_BIT_ARM;

	std::array<VkDataGraphPipelineConstantARM, 2> constants = {
	    VkDataGraphPipelineConstantARM{ VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_CONSTANT_ARM, &weights_desc, 0, weights_data.data() },
	    VkDataGraphPipelineConstantARM{ VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_CONSTANT_ARM, &bias_desc, 1, bias_data.data() },
	};

	VkShaderModule graph_shader = create_shader_module(vulkan, vulkan_tensors_4_conv2d_spv, vulkan_tensors_4_conv2d_spv_len);

	std::array<VkDataGraphPipelineResourceInfoARM, 2> resource_infos = {};
	resource_infos[0].sType = VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_RESOURCE_INFO_ARM;
	resource_infos[0].pNext = &input_tensor.description;
	resource_infos[0].descriptorSet = 0;
	resource_infos[0].binding = 0;
	resource_infos[0].arrayElement = 0;
	resource_infos[1].sType = VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_RESOURCE_INFO_ARM;
	resource_infos[1].pNext = &output_tensor.description;
	resource_infos[1].descriptorSet = 0;
	resource_infos[1].binding = 1;
	resource_infos[1].arrayElement = 0;

	VkDataGraphPipelineShaderModuleCreateInfoARM shader_info = {
	    VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SHADER_MODULE_CREATE_INFO_ARM, nullptr };
	shader_info.module = graph_shader;
	shader_info.pName = "main";
	shader_info.constantCount = static_cast<uint32_t>(constants.size());
	shader_info.pConstants = constants.data();

	VkDataGraphPipelineCreateInfoARM pipeline_info = { VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_CREATE_INFO_ARM, &shader_info };
	pipeline_info.flags = 0;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.resourceInfoCount = static_cast<uint32_t>(resource_infos.size());
	pipeline_info.pResourceInfos = resource_infos.data();

	VkPipeline data_graph_pipeline = VK_NULL_HANDLE;
	result = graph_funcs.create_pipelines(vulkan.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &data_graph_pipeline);
	check(result);

	data_graph_session graph_session = create_data_graph_session(vulkan, graph_funcs, data_graph_pipeline);

	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	result = vkCreateCommandPool(vulkan.device, &pool_create_info, nullptr, &command_pool);
	check(result);

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo cmd_alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	cmd_alloc_info.commandPool = command_pool;
	cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &cmd_alloc_info, &command_buffer);
	check(result);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_DATA_GRAPH_ARM, data_graph_pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_DATA_GRAPH_ARM, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
	graph_funcs.cmd_dispatch(command_buffer, graph_session.session, nullptr);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);

	destroy_data_graph_session(vulkan, graph_funcs, graph_session);
	vkDestroyPipeline(vulkan.device, data_graph_pipeline, nullptr);
	vkDestroyShaderModule(vulkan.device, graph_shader, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyDescriptorPool(vulkan.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, set_layout, nullptr);

	destroy_tensor(vulkan, tensor_funcs, output_tensor);
	destroy_tensor(vulkan, tensor_funcs, input_tensor);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);
	return 0;
}
