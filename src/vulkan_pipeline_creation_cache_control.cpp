// Minimal test for VK_EXT_pipeline_creation_cache_control

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// Reuse existing compute shader
#include "vulkan_compute_1.inc"

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
	p__loops = 1;
	vulkan_req_t req{};
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.apiVersion = VK_API_VERSION_1_3;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.device_extensions.push_back("VK_EXT_pipeline_creation_cache_control");
	req.options["allow_compile_required"] = true;
	req.reqfeat13.pipelineCreationCacheControl = VK_TRUE;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_pipeline_creation_cache_control", req);

	compute_resources r = compute_init(vulkan, req);
	r.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
	descriptorSetLayoutBinding.binding = 0;
	descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorSetLayoutBinding.descriptorCount = 1;
	descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	descriptorSetLayoutCreateInfo.bindingCount = 1;
	descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;
	VkResult result = vkCreateDescriptorSetLayout(vulkan.device, &descriptorSetLayoutCreateInfo, nullptr, &r.descriptorSetLayout);
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
	VkWriteDescriptorSet writeDescriptorSet = {};
	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.dstSet = r.descriptorSet;
	writeDescriptorSet.dstBinding = 0;
	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
	vkUpdateDescriptorSets(vulkan.device, 1, &writeDescriptorSet, 0, NULL);

	compute_create_pipeline(vulkan, r, req, VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT);
	if (r.pipeline == VK_NULL_HANDLE)
	{
		printf("Skipping: pipeline requires compilation\n");
		compute_done(vulkan, r, req);
		test_done(vulkan);
		return 77;
	}

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
	check(result);

	vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
	vkCmdBindDescriptorSets(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, nullptr);

	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	const int workgroup_size = std::get<int>(req.options.at("wg_size"));
	vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);

	result = vkEndCommandBuffer(r.commandBuffer);
	check(result);

	compute_submit(vulkan, r, req);
	compute_done(vulkan, r, req);
	test_done(vulkan);
	return 0;
}
