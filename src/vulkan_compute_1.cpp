// Unit test to try out vulkan compute with variations
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

#include <cmath>

static int fence_variant = 0;
static bool indirect = false;
static int indirectOffset = 0;

struct pushconstants
{
	float width;
	float height;
};

static void show_usage()
{
	compute_usage();
	printf("-f/--fence-variant N   Set fence variant (default %d)\n", fence_variant);
	printf("\t0 - use vkWaitForFences\n");
	printf("\t1 - use vkGetFenceStatus\n");
	printf("-I/--indirect          Use indirect compute dispatch (default %d)\n", (int)indirect);
	printf("  -ioff N              Use indirect compute dispatch buffer with this offset multiple (default %d)\n", indirectOffset);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-f", "--fence-variant"))
	{
		fence_variant = get_arg(argv, ++i, argc);
		return (fence_variant >= 0 && fence_variant <= 1);
	}
	else if (match(argv[i], "-ioff", "--indirect-offset"))
	{
		indirectOffset = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-I", "--indirect"))
	{
		indirect = true;
		return true;
	}
	return compute_cmdopt(i, argc, argv, reqs);
}

static void waitfence(vulkan_setup_t& vulkan, VkFence fence)
{
	if (fence_variant == 0)
	{
		VkResult result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT32_MAX);
		check(result);
	}
	else if (fence_variant == 1)
	{
		VkResult result = VK_NOT_READY;
		do
		{
			result = vkGetFenceStatus(vulkan.device, fence);
			assert(result != VK_ERROR_DEVICE_LOST);
			if (result != VK_SUCCESS) usleep(10);
		} while (result != VK_SUCCESS);
	}
}

int main(int argc, char** argv)
{
	vulkan_req_t req;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_1", req);
	compute_resources r = compute_init(vulkan, req);
	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	const int workgroup_size = std::get<int>(req.options.at("wg_size"));
	VkResult result;

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
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
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
	vkUpdateDescriptorSets(vulkan.device, 1, &writeDescriptorSet, 0, NULL);

	if (indirect)
	{
		VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
		bufferCreateInfo.size = sizeof(VkDispatchIndirectCommand) * (1 + indirectOffset);
		bufferCreateInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &r.indirectBuffer);
		assert(result == VK_SUCCESS);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan.device, r.indirectBuffer, &memory_requirements);
		const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
		const uint32_t aligned_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);

		VkMemoryAllocateFlagsInfo flaginfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };
		VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
		if (vulkan.apiVersion >= VK_API_VERSION_1_1)
		{
			pAllocateMemInfo.pNext = &flaginfo;
		}
		pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
		pAllocateMemInfo.allocationSize = aligned_size * (indirectOffset + 1);
		result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &r.indirectMemory);
		check(result);
		assert(r.indirectMemory != VK_NULL_HANDLE);

		result = vkBindBufferMemory(vulkan.device, r.indirectBuffer, r.indirectMemory, indirectOffset * aligned_size);
		check(result);

		uint32_t* data = nullptr;
		result = vkMapMemory(vulkan.device, r.indirectMemory, indirectOffset * aligned_size, aligned_size, 0, (void**)&data);
		assert(result == VK_SUCCESS);
		data[0] = ceil(width / float(workgroup_size));
		data[1] = ceil(height / float(workgroup_size));
		data[2] = 1;
		vkUnmapMemory(vulkan.device, r.indirectMemory);
	}

	uint32_t code_size = long(ceil(vulkan_compute_1_spirv_len / 4.0)) * 4;
	r.code.resize(code_size);
	memcpy(r.code.data(), vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);

	compute_create_pipeline(vulkan, r, req);

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
	check(result);
	vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
	vkCmdBindDescriptorSets(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, NULL);
	if (indirect)
	{
		vkCmdDispatchIndirect(r.commandBuffer, r.indirectBuffer, indirectOffset * sizeof(VkDispatchIndirectCommand));
	}
	else
	{
		vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
	}
	result = vkEndCommandBuffer(r.commandBuffer);
	check(result);

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &r.commandBuffer;

	VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	fenceCreateInfo.flags = 0;
	result = vkCreateFence(vulkan.device, &fenceCreateInfo, NULL, &r.fence);
	check(result);

	result = vkQueueSubmit(r.queue, 1, &submitInfo, r.fence);
	check(result);
	waitfence(vulkan, r.fence);

	compute_done(vulkan, r, req);
	vkDestroyFence(vulkan.device, r.fence, NULL);
	vkDestroyBuffer(vulkan.device, r.buffer, NULL);
	if (indirect) vkDestroyBuffer(vulkan.device, r.indirectBuffer, NULL);
	testFreeMemory(vulkan, r.memory);
	if (indirect) testFreeMemory(vulkan, r.indirectMemory);
	vkDestroyShaderModule(vulkan.device, r.computeShaderModule, NULL);
	vkDestroyDescriptorPool(vulkan.device, r.descriptorPool, NULL);
	vkDestroyDescriptorSetLayout(vulkan.device, r.descriptorSetLayout, NULL);
	vkDestroyPipelineLayout(vulkan.device, r.pipelineLayout, NULL);
	vkDestroyPipeline(vulkan.device, r.pipeline, NULL);
	vkDestroyCommandPool(vulkan.device, r.commandPool, NULL);

	test_done(vulkan);
}
