// Minimal tensor compute test based on Vulkan-Samples compute_shaders_with_tensors.
// Shaders generated with:
//   xxd -i -n vulkan_tensors_2_preprocessing_spv content/vulkan-samples/shaders/tensor_and_data_graph/compute_shaders_with_tensors/glsl/preprocessing.comp.spv > src/vulkan_tensors_2_preprocessing.inc
//   xxd -i -n vulkan_tensors_2_postprocessing_spv content/vulkan-samples/shaders/tensor_and_data_graph/compute_shaders_with_tensors/glsl/postprocessing.comp.spv > src/vulkan_tensors_2_postprocessing.inc

#include "vulkan_common.h"

#include <array>

#include "vulkan_tensors_2_preprocessing.inc"
#include "vulkan_tensors_2_postprocessing.inc"

struct tensor_resource
{
	VkTensorARM tensor = VK_NULL_HANDLE;
	VkTensorViewARM view = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
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
                                     const std::array<int64_t, 4>& dimensions, VkTensorUsageFlagsARM usage, VkFormat format)
{
	tensor_resource res;
	VkTensorDescriptionARM desc = { VK_STRUCTURE_TYPE_TENSOR_DESCRIPTION_ARM, nullptr };
	desc.tiling = VK_TENSOR_TILING_LINEAR_ARM;
	desc.format = format;
	desc.dimensionCount = static_cast<uint32_t>(dimensions.size());
	desc.pDimensions = dimensions.data();
	desc.pStrides = nullptr;
	desc.usage = usage;

	VkTensorCreateInfoARM create_info = { VK_STRUCTURE_TYPE_TENSOR_CREATE_INFO_ARM, nullptr };
	create_info.flags = 0;
	create_info.pDescription = &desc;
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
	const uint32_t memory_type_index = get_device_memory_type(memreq.memoryRequirements.memoryTypeBits, 0);

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

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	VkPhysicalDeviceTensorFeaturesARM tensor_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM, nullptr };
	tensor_features.tensors = VK_TRUE;
	tensor_features.shaderTensorAccess = VK_TRUE;
	tensor_features.pNext = reqs.extension_features;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&tensor_features);
	reqs.device_extensions.push_back("VK_ARM_tensors");
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.apiVersion = VK_API_VERSION_1_3;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_tensors_2", reqs);

	bench_start_iteration(vulkan.bench);

	MAKEDEVICEPROCADDR(vulkan, vkCreateTensorARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyTensorARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetTensorMemoryRequirementsARM);
	MAKEDEVICEPROCADDR(vulkan, vkCreateTensorViewARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyTensorViewARM);
	MAKEDEVICEPROCADDR(vulkan, vkBindTensorMemoryARM);

	tensor_functions funcs = {
	    pf_vkCreateTensorARM,
	    pf_vkDestroyTensorARM,
	    pf_vkGetTensorMemoryRequirementsARM,
	    pf_vkBindTensorMemoryARM,
	    pf_vkCreateTensorViewARM,
	    pf_vkDestroyTensorViewARM,
	};

	const uint32_t width = 100;
	const uint32_t height = 100;
	const std::array<int64_t, 4> dims = { 1, static_cast<int64_t>(height), static_cast<int64_t>(width), 3 };

	tensor_resource pre_tensor = create_tensor(
	    vulkan, funcs, dims, VK_TENSOR_USAGE_SHADER_BIT_ARM, VK_FORMAT_R32_SFLOAT);
	tensor_resource post_tensor = create_tensor(
	    vulkan, funcs, dims, VK_TENSOR_USAGE_SHADER_BIT_ARM, VK_FORMAT_R32_SFLOAT);

	VkDescriptorSetLayoutBinding pre_binding = {};
	pre_binding.binding = 0;
	pre_binding.descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
	pre_binding.descriptorCount = 1;
	pre_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo pre_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	pre_layout_info.bindingCount = 1;
	pre_layout_info.pBindings = &pre_binding;
	VkDescriptorSetLayout pre_layout = VK_NULL_HANDLE;
	VkResult result = vkCreateDescriptorSetLayout(vulkan.device, &pre_layout_info, nullptr, &pre_layout);
	check(result);

	std::array<VkDescriptorSetLayoutBinding, 2> post_bindings = {};
	post_bindings[0] = pre_binding;
	post_bindings[1] = pre_binding;
	post_bindings[1].binding = 1;

	VkDescriptorSetLayoutCreateInfo post_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	post_layout_info.bindingCount = static_cast<uint32_t>(post_bindings.size());
	post_layout_info.pBindings = post_bindings.data();
	VkDescriptorSetLayout post_layout = VK_NULL_HANDLE;
	result = vkCreateDescriptorSetLayout(vulkan.device, &post_layout_info, nullptr, &post_layout);
	check(result);

	VkDescriptorPoolSize pool_size = {};
	pool_size.type = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
	pool_size.descriptorCount = 3;
	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	pool_info.maxSets = 2;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	result = vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &descriptor_pool);
	check(result);

	VkDescriptorSet pre_set = VK_NULL_HANDLE;
	VkDescriptorSet post_set = VK_NULL_HANDLE;
	VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	alloc_info.descriptorPool = descriptor_pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &pre_layout;
	result = vkAllocateDescriptorSets(vulkan.device, &alloc_info, &pre_set);
	check(result);
	alloc_info.pSetLayouts = &post_layout;
	result = vkAllocateDescriptorSets(vulkan.device, &alloc_info, &post_set);
	check(result);

	VkWriteDescriptorSetTensorARM pre_tensor_info = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_TENSOR_ARM, nullptr };
	pre_tensor_info.tensorViewCount = 1;
	pre_tensor_info.pTensorViews = &pre_tensor.view;
	VkWriteDescriptorSet pre_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &pre_tensor_info };
	pre_write.dstSet = pre_set;
	pre_write.dstBinding = 0;
	pre_write.descriptorCount = 1;
	pre_write.descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;

	VkWriteDescriptorSetTensorARM post_tensor_infos[2] = {
	    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_TENSOR_ARM, nullptr, 1, &pre_tensor.view },
	    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_TENSOR_ARM, nullptr, 1, &post_tensor.view }
	};
	VkWriteDescriptorSet post_writes[2] = {
	    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &post_tensor_infos[0] },
	    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &post_tensor_infos[1] }
	};
	post_writes[0].dstSet = post_set;
	post_writes[0].dstBinding = 0;
	post_writes[0].descriptorCount = 1;
	post_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
	post_writes[1].dstSet = post_set;
	post_writes[1].dstBinding = 1;
	post_writes[1].descriptorCount = 1;
	post_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;

	vkUpdateDescriptorSets(vulkan.device, 1, &pre_write, 0, nullptr);
	vkUpdateDescriptorSets(vulkan.device, 2, post_writes, 0, nullptr);

	VkPushConstantRange push_range = {};
	push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_range.offset = 0;
	push_range.size = sizeof(float);

	VkPipelineLayoutCreateInfo pre_pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pre_pipeline_layout_info.setLayoutCount = 1;
	pre_pipeline_layout_info.pSetLayouts = &pre_layout;
	pre_pipeline_layout_info.pushConstantRangeCount = 1;
	pre_pipeline_layout_info.pPushConstantRanges = &push_range;
	VkPipelineLayout pre_pipeline_layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(vulkan.device, &pre_pipeline_layout_info, nullptr, &pre_pipeline_layout);
	check(result);

	VkPipelineLayoutCreateInfo post_pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	post_pipeline_layout_info.setLayoutCount = 1;
	post_pipeline_layout_info.pSetLayouts = &post_layout;
	post_pipeline_layout_info.pushConstantRangeCount = 1;
	post_pipeline_layout_info.pPushConstantRanges = &push_range;
	VkPipelineLayout post_pipeline_layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(vulkan.device, &post_pipeline_layout_info, nullptr, &post_pipeline_layout);
	check(result);

	VkShaderModule pre_shader = create_shader_module(vulkan, vulkan_tensors_2_preprocessing_spv, vulkan_tensors_2_preprocessing_spv_len);
	VkShaderModule post_shader = create_shader_module(vulkan, vulkan_tensors_2_postprocessing_spv, vulkan_tensors_2_postprocessing_spv_len);

	VkPipeline pre_pipeline = VK_NULL_HANDLE;
	VkComputePipelineCreateInfo pre_pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	pre_pipeline_info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	pre_pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pre_pipeline_info.stage.module = pre_shader;
	pre_pipeline_info.stage.pName = "main";
	pre_pipeline_info.layout = pre_pipeline_layout;
	result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pre_pipeline_info, nullptr, &pre_pipeline);
	check(result);

	VkPipeline post_pipeline = VK_NULL_HANDLE;
	VkComputePipelineCreateInfo post_pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	post_pipeline_info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	post_pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	post_pipeline_info.stage.module = post_shader;
	post_pipeline_info.stage.pName = "main";
	post_pipeline_info.layout = post_pipeline_layout;
	result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &post_pipeline_info, nullptr, &post_pipeline);
	check(result);

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

	const float time_seconds = 0.25f;
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pre_pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pre_pipeline_layout, 0, 1, &pre_set, 0, nullptr);
	vkCmdPushConstants(command_buffer, pre_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(time_seconds), &time_seconds);
	vkCmdDispatch(command_buffer, width, height, 1);

	VkTensorMemoryBarrierARM tensor_barrier = { VK_STRUCTURE_TYPE_TENSOR_MEMORY_BARRIER_ARM, nullptr };
	tensor_barrier.tensor = pre_tensor.tensor;
	tensor_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	tensor_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	tensor_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	tensor_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	VkDependencyInfo dependency_info = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, &tensor_barrier };
	vkCmdPipelineBarrier2(command_buffer, &dependency_info);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, post_pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, post_pipeline_layout, 0, 1, &post_set, 0, nullptr);
	vkCmdPushConstants(command_buffer, post_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(time_seconds), &time_seconds);
	vkCmdDispatch(command_buffer, width, height, 1);

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

	vkDestroyPipeline(vulkan.device, post_pipeline, nullptr);
	vkDestroyPipeline(vulkan.device, pre_pipeline, nullptr);
	vkDestroyShaderModule(vulkan.device, post_shader, nullptr);
	vkDestroyShaderModule(vulkan.device, pre_shader, nullptr);
	vkDestroyPipelineLayout(vulkan.device, post_pipeline_layout, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pre_pipeline_layout, nullptr);
	vkDestroyDescriptorPool(vulkan.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, post_layout, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, pre_layout, nullptr);

	destroy_tensor(vulkan, funcs, post_tensor);
	destroy_tensor(vulkan, funcs, pre_tensor);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);
	return 0;
}
