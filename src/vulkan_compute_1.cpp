// Unit test to try out vulkan compute with variations
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

static bool indirect = false;
static int indirectOffset = 0; // in units of indirect structs

struct pushconstants
{
	float width;
	float height;
};

static void show_usage()
{
	compute_usage();
	printf("-I/--indirect          Use indirect compute dispatch (default %d)\n", (int)indirect);
	printf("  -ioff N              Use indirect compute dispatch buffer with this offset multiple (default %d)\n", indirectOffset);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-ioff", "--indirect-offset"))
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

int main(int argc, char** argv)
{
	p__loops = 2; // default to 2 loops
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

	VkDeviceMemory indirectMemory = VK_NULL_HANDLE;
	VkBuffer indirectBuffer = VK_NULL_HANDLE;

	const int start_offset = indirectOffset * sizeof(VkDispatchIndirectCommand);
	if (indirect)
	{
		VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
		bufferCreateInfo.size = sizeof(VkDispatchIndirectCommand) * (1 + indirectOffset);
		bufferCreateInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &indirectBuffer);
		assert(result == VK_SUCCESS);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan.device, indirectBuffer, &memory_requirements);
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
		pAllocateMemInfo.allocationSize = aligned_size;
		result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &indirectMemory);
		check(result);
		assert(indirectMemory != VK_NULL_HANDLE);

		result = vkBindBufferMemory(vulkan.device, indirectBuffer, indirectMemory, 0);
		check(result);

		uint32_t* data = nullptr;
		result = vkMapMemory(vulkan.device, indirectMemory, start_offset, VK_WHOLE_SIZE, 0, (void**)&data);
		assert(result == VK_SUCCESS);
		data[0] = ceil(width / float(workgroup_size));
		data[1] = ceil(height / float(workgroup_size));
		data[2] = 1;
		testFlushMemory(vulkan, indirectMemory, start_offset, VK_WHOLE_SIZE, vulkan.has_explicit_host_updates);
		vkUnmapMemory(vulkan.device, indirectMemory);
	}

	r.code = copy_shader(vulkan_compute_1_spirv,vulkan_compute_1_spirv_len);

	compute_create_pipeline(vulkan, r, req);

	for (int frame = 0; frame < p__loops; frame++)
	{
		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vulkan.garbage_pointers) beginInfo.pInheritanceInfo = (const VkCommandBufferInheritanceInfo*)0xdeadbeef; // tools must ignore this
		result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
		check(result);
		vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
		vkCmdBindDescriptorSets(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, NULL);
		if (indirect)
		{
			vkCmdDispatchIndirect(r.commandBuffer, indirectBuffer, start_offset);
		}
		else
		{
			vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
		}
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);

		compute_submit(vulkan, r, req);
	}

	if (indirect)
	{
		vkDestroyBuffer(vulkan.device, indirectBuffer, NULL);
		testFreeMemory(vulkan, indirectMemory);
	}
	compute_done(vulkan, r, req);
	test_done(vulkan);
}
