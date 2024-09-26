// Minimal compute unit test
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_bda_ubo.comp -o vulkan_compute_bda_ubo.spirv
//   xxd -i vulkan_compute_bda_ubo.spirv > vulkan_compute_bda_ubo.inc
#include "vulkan_compute_bda_ubo.inc"
#include "vulkan_compute_bda_ssbo.inc"

#include <cmath>

#define UBO_SIZE 32

static bool use_ssbo = false;

struct pixel
{
	float r, g, b, a;
};

static void show_usage()
{
	compute_usage();
	printf("-S/--ssbo              Use SSBO instead of UBO\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-S", "--ssbo"))
	{
		use_ssbo = true;
		return true;
	}
	return compute_cmdopt(i, argc, argv, reqs);
}

int main(int argc, char** argv)
{
	p__loops = 3; // default to 3 loops
	vulkan_req_t reqs;
	reqs.options["width"] = 640;
	reqs.options["height"] = 480;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_bda_ubo", reqs);
	compute_resources r = compute_init(vulkan, reqs);
	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int workgroup_size = std::get<int>(reqs.options.at("wg_size"));
	VkBuffer ubo = VK_NULL_HANDLE;
	VkDeviceMemory ubo_memory = VK_NULL_HANDLE;
	VkResult result;

	// UBO handling

	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = UBO_SIZE; // just need 8 bytes, but want to have space to have fun too
	if (use_ssbo)
	{
		bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	else
	{
		bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &ubo);
	assert(result == VK_SUCCESS);
	assert(ubo);

	VkMemoryRequirements memory_requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, ubo, &memory_requirements);
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
	const uint32_t aligned_buffer_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_buffer_size;
	VkMemoryAllocateFlagsInfo flaginfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };
	pAllocateMemInfo.pNext = &flaginfo;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &ubo_memory);
	check(result);
	assert(ubo_memory);

	result = vkBindBufferMemory(vulkan.device, ubo, ubo_memory, 0);
	check(result);

	VkBufferDeviceAddressInfo bdainfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
	bdainfo.buffer = r.buffer;
	VkDeviceAddress address = vkGetBufferDeviceAddress(vulkan.device, &bdainfo);

	if (!vulkan.has_trace_helpers)
	{
		char* data = nullptr;
		result = vkMapMemory(vulkan.device, ubo_memory, 0, aligned_buffer_size, 0, (void**)&data);
		*((uint64_t*)data) = address;
		vkUnmapMemory(vulkan.device, ubo_memory);
	}
	else
	{
		VkDeviceSize offset = 0;
		VkAddressRemapTRACETOOLTEST ar = { VK_STRUCTURE_TYPE_ADDRESS_REMAP_TRACETOOLTEST, nullptr };
		ar.target = VK_ADDRESS_REMAP_TARGET_BUFFER_TRACETOOLTEST;
		ar.count = 1;
		ar.pOffsets = &offset;
		VkUpdateMemoryInfoTRACETOOLTEST ui = { VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_TRACETOOLTEST, &ar };
		ui.dstOffset = 0;
		ui.dataSize = 8;
		ui.pData = &address;
		vulkan.vkUpdateBuffer(vulkan.device, r.buffer, &ui);
	}

	// generic setup

	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
	descriptorSetLayoutBinding.binding = 0;
	if (use_ssbo)
	{
		descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	}
	else
	{
		descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	}
	descriptorSetLayoutBinding.descriptorCount = 1;
	descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	descriptorSetLayoutCreateInfo.bindingCount = 1;
	descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;
        result = vkCreateDescriptorSetLayout(vulkan.device, &descriptorSetLayoutCreateInfo, nullptr, &r.descriptorSetLayout);
	check(result);

	VkDescriptorPoolSize descriptorPoolSize = {};
	if (use_ssbo)
	{
		descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	}
	else
	{
		descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	}
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
	descriptorBufferInfo.buffer = ubo;
	descriptorBufferInfo.offset = 0;
	descriptorBufferInfo.range = UBO_SIZE;
	VkWriteDescriptorSet writeDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	writeDescriptorSet.dstSet = r.descriptorSet;
	writeDescriptorSet.dstBinding = 0;
	writeDescriptorSet.descriptorCount = 1;
	if (use_ssbo)
	{
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	}
	else
	{
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	}
	writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
	vkUpdateDescriptorSets(vulkan.device, 1, &writeDescriptorSet, 0, NULL);

	if (use_ssbo)
	{
		uint32_t code_size = long(ceil(vulkan_compute_bda_ssbo_spirv_len / 4.0)) * 4;
		r.code.resize(code_size);
		memcpy(r.code.data(), vulkan_compute_bda_ssbo_spirv, vulkan_compute_bda_ssbo_spirv_len);
	}
	else
	{
		uint32_t code_size = long(ceil(vulkan_compute_bda_ubo_spirv_len / 4.0)) * 4;
		r.code.resize(code_size);
		memcpy(r.code.data(), vulkan_compute_bda_ubo_spirv, vulkan_compute_bda_ubo_spirv_len);
	}

	compute_create_pipeline(vulkan, r, reqs);

	for (int frame = 0; frame < p__loops; frame++)
	{
		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
		check(result);
		vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
		vkCmdBindDescriptorSets(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, NULL);
		vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);

		compute_submit(vulkan, r, reqs);
	}

	compute_done(vulkan, r, reqs);
	vkDestroyBuffer(vulkan.device, ubo, nullptr);
	vkFreeMemory(vulkan.device, ubo_memory, nullptr);
	test_done(vulkan);
}
