// Minimal compute unit test using VK_EXT_shader_object
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

struct pixel
{
	float r, g, b, a;
};

static void show_usage()
{
	compute_usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return compute_cmdopt(i, argc, argv, reqs);
}

int main(int argc, char** argv)
{
	p__loops = 1; // default to one loop
	VkPhysicalDeviceShaderObjectFeaturesEXT shader_object_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT, nullptr };
	shader_object_features.shaderObject = VK_TRUE;
	vulkan_req_t req;
	req.options["width"] = 640;
	req.options["height"] = 480;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.apiVersion = VK_API_VERSION_1_3;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.reqfeat13.dynamicRendering = VK_TRUE;
	req.device_extensions.push_back("VK_EXT_shader_object");
	req.extension_features = (VkBaseInStructure*)&shader_object_features;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_shader_object", req);
	compute_resources r = compute_init(vulkan, req);
	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	const int workgroup_size = std::get<int>(req.options.at("wg_size"));
	VkResult result;

	MAKEDEVICEPROCADDR(vulkan, vkCreateShadersEXT);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyShaderEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBindShadersEXT);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
	descriptorSetLayoutBinding.binding = 0;
	descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorSetLayoutBinding.descriptorCount = 1;
	descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	descriptorSetLayoutCreateInfo.bindingCount = 1;
	descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;
	result = vkCreateDescriptorSetLayout(vulkan.device, &descriptorSetLayoutCreateInfo, nullptr, &r.descriptorSetLayout);
	check(result);

	VkDescriptorPoolSize descriptorPoolSize = {};
	descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorPoolSize.descriptorCount = 1;
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	descriptorPoolCreateInfo.maxSets = 1;
	descriptorPoolCreateInfo.poolSizeCount = 1;
	descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
	result = vkCreateDescriptorPool(vulkan.device, &descriptorPoolCreateInfo, nullptr, &r.descriptorPool);
	check(result);

	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	descriptorSetAllocateInfo.descriptorPool = r.descriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = 1;
	descriptorSetAllocateInfo.pSetLayouts = &r.descriptorSetLayout;
	result = vkAllocateDescriptorSets(vulkan.device, &descriptorSetAllocateInfo, &r.descriptorSet);
	check(result);

	VkDescriptorBufferInfo descriptorBufferInfo = {};
	descriptorBufferInfo.buffer = r.buffer;
	descriptorBufferInfo.offset = 0;
	descriptorBufferInfo.range = r.buffer_size;
	VkWriteDescriptorSet writeDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	writeDescriptorSet.dstSet = r.descriptorSet;
	writeDescriptorSet.dstBinding = 0;
	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
	vkUpdateDescriptorSets(vulkan.device, 1, &writeDescriptorSet, 0, nullptr);

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipelineLayoutCreateInfo.setLayoutCount = 1;
	pipelineLayoutCreateInfo.pSetLayouts = &r.descriptorSetLayout;
	result = vkCreatePipelineLayout(vulkan.device, &pipelineLayoutCreateInfo, nullptr, &r.pipelineLayout);
	check(result);

	std::vector<VkSpecializationMapEntry> smentries(5);
	for (unsigned i = 0; i < smentries.size(); i++)
	{
		smentries[i].constantID = i;
		smentries[i].offset = i * 4;
		smentries[i].size = 4;
	}

	std::vector<int32_t> sdata(5);
	sdata[0] = workgroup_size;
	sdata[1] = workgroup_size;
	sdata[2] = 1;
	sdata[3] = width;
	sdata[4] = height;

	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = smentries.size();
	specInfo.pMapEntries = smentries.data();
	specInfo.dataSize = sdata.size() * 4;
	specInfo.pData = sdata.data();

	r.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);
	VkShaderCreateInfoEXT shaderCreateInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT, nullptr };
	shaderCreateInfo.flags = 0;
	shaderCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderCreateInfo.nextStage = 0;
	shaderCreateInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
	shaderCreateInfo.codeSize = r.code.size() * sizeof(uint32_t);
	shaderCreateInfo.pCode = r.code.data();
	shaderCreateInfo.pName = "main";
	shaderCreateInfo.setLayoutCount = 1;
	shaderCreateInfo.pSetLayouts = &r.descriptorSetLayout;
	shaderCreateInfo.pushConstantRangeCount = 0;
	shaderCreateInfo.pPushConstantRanges = nullptr;
	shaderCreateInfo.pSpecializationInfo = &specInfo;

	VkShaderEXT shader = VK_NULL_HANDLE;
	result = pf_vkCreateShadersEXT(vulkan.device, 1, &shaderCreateInfo, nullptr, &shader);
	check(result);

	for (int frame = 0; frame < p__loops; frame++)
	{
		test_marker(vulkan, "Frame " + std::to_string(frame));
		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
		check(result);
		VkShaderStageFlagBits stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pf_vkCmdBindShadersEXT(r.commandBuffer, 1, &stage, &shader);
		vkCmdBindDescriptorSets(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, nullptr);
		vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);

		compute_submit(vulkan, r, req);
	}

	pf_vkDestroyShaderEXT(vulkan.device, shader, nullptr);
	if (r.image) vkDestroyImage(vulkan.device, r.image, nullptr);
	vkDestroyBuffer(vulkan.device, r.buffer, nullptr);
	testFreeMemory(vulkan, r.memory);
	vkDestroyDescriptorPool(vulkan.device, r.descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, r.descriptorSetLayout, nullptr);
	vkDestroyPipelineLayout(vulkan.device, r.pipelineLayout, nullptr);
	vkDestroyCommandPool(vulkan.device, r.commandPool, nullptr);
	test_done(vulkan);
}
