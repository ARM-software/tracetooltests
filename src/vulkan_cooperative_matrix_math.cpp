#include "vulkan_common.h"

#include "vulkan_cooperative_matrix_math.inc"
#include <vector>
#include <array>
#include <cstdio>
#include <cmath>

static void show_usage(){}

static bool test_cmdopt(int& i, int argc, char **argv, vulkan_req_t& reqs)
{
	return false;
}

typedef struct alignas(32) MatrixData {
	int32_t signed_values[256];
	uint32_t unsigned_values[256];
} MatrixData;

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.device_extensions.push_back("VK_KHR_cooperative_matrix");
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.reqfeat12.vulkanMemoryModel = VK_TRUE;

	VkPhysicalDeviceCooperativeMatrixFeaturesKHR coopFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR,
		.pNext = reqs.extension_features,
		.cooperativeMatrix = VK_TRUE,
		.cooperativeMatrixRobustBufferAccess = VK_FALSE,
	};
	reqs.extension_features = (VkBaseInStructure*)&coopFeatures;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vk = test_init(argc, argv, "vulkan_cooperative_matrix_math", reqs);

	MAKEINSTANCEPROCADDR(vk, vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR);

	// Capability check
	uint32_t propCount = 0;
	VkResult r = pf_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(vk.physical, &propCount, nullptr);
	if (r == VK_ERROR_EXTENSION_NOT_PRESENT)
	{
		test_done(vk);
		return 77;
	}

	bench_start_iteration(vk.bench);

	std::vector<VkCooperativeMatrixPropertiesKHR> coopProps =
		std::vector<VkCooperativeMatrixPropertiesKHR>(propCount, 
			{.sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR,
			 .pNext = nullptr
			});
	
	if (propCount > 0)
	{
		r = pf_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(vk.physical, &propCount, coopProps.data());
		check(r);
	}
	VkPhysicalDeviceCooperativeMatrixPropertiesKHR physicalCoopProps = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR,
		.pNext = nullptr,
	};
	VkPhysicalDeviceProperties2 physicalDeviceProps = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &physicalCoopProps,
	};
	vkGetPhysicalDeviceProperties2(vk.physical, &physicalDeviceProps);

	if ((physicalCoopProps.cooperativeMatrixSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
	{
		printf("Skipping: cooperative matrix not supported for compute stage\n");
		bench_stop_iteration(vk.bench);
		test_done(vk);
		return 77;
	}

	// check that the coopMat size variant in shader is supported
	bool variant = false;
	for (const VkCooperativeMatrixPropertiesKHR &p : coopProps)
	{
		if (p.scope != VK_SCOPE_SUBGROUP_KHR)
			continue;
		if (p.MSize != 16 || p.NSize != 16 || p.KSize != 16)
			continue;
		variant = true;
		break;
	}
	if (!variant)
	{
		printf("Skipping: no supported cooperative matrix mode matches available shader variants\n");
		for (const auto &p : coopProps)
		{
			printf("\tsupported mode: M=%u N=%u K=%u, A=%u B=%u C=%u Result=%u, scope=0x%x\n",
			       p.MSize, p.NSize, p.KSize, p.AType, p.BType, p.CType, p.ResultType, p.scope);
		}
		bench_stop_iteration(vk.bench);
		test_done(vk);
		return 77;
	}
	printf("Using cooperative matrix mode M=16 N=16 K=16\n");

	std::array<VkBuffer, 2> ssbo{VK_NULL_HANDLE};
	std::array<VkDeviceMemory, 2> ssboMemory{VK_NULL_HANDLE};

	const VkDeviceSize bufferSize = sizeof(MatrixData);

	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = bufferSize,
		.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	for (uint32_t i = 0; i < ssbo.size(); ++i)
	{
		r = vkCreateBuffer(vk.device, &bufferInfo, nullptr, &ssbo[i]);
		check(r);
		VkMemoryRequirements ssboMemReq = {};
		vkGetBufferMemoryRequirements(vk.device, ssbo[i], &ssboMemReq);
		VkPhysicalDeviceMemoryProperties ssboMemTypeProp;
		vkGetPhysicalDeviceMemoryProperties(vk.physical, &ssboMemTypeProp);

		VkMemoryAllocateInfo ssboMemAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = nullptr,
			.allocationSize = ssboMemReq.size,
			.memoryTypeIndex = get_device_memory_type(ssboMemReq.memoryTypeBits,
					VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
		};

		r = vkAllocateMemory(vk.device, &ssboMemAllocInfo, nullptr, &ssboMemory[i]);
		check(r);
		r = vkBindBufferMemory(vk.device, ssbo[i], ssboMemory[i], 0);
		check(r);
	}

	// copy data to buffer memory
	MatrixData mData;
	for(size_t i = 0; i < 256; ++i)
	{
		mData.signed_values[i] = -2;
		mData.unsigned_values[i] = 3;
	}
	void* data;
	vkMapMemory(vk.device, ssboMemory[0], 0, bufferSize, 0x0, &data);
	memcpy(data, &mData, (size_t)bufferSize);
	vkUnmapMemory(vk.device, ssboMemory[0]);

	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	std::array<VkDescriptorSetLayoutBinding, 2> layoutBindings{};
	layoutBindings[0].binding = 0;
	layoutBindings[0].descriptorCount = 1;
	layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	layoutBindings[0].pImmutableSamplers = nullptr;
	layoutBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	layoutBindings[1].binding = 1;
	layoutBindings[1].descriptorCount = 1;
	layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	layoutBindings[1].pImmutableSamplers = nullptr;
	layoutBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	
	VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0x0,
		.bindingCount = 2,
		.pBindings = layoutBindings.data(),
	};

	r = vkCreateDescriptorSetLayout(vk.device, &layoutCreateInfo, nullptr, &descriptorSetLayout);
	check(r);

	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorPoolSize poolSize{
		.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 2,
	};
	
	VkDescriptorPoolCreateInfo poolCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0x0,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &poolSize,
	};

	r = vkCreateDescriptorPool(vk.device, &poolCreateInfo, nullptr, &pool);
	check(r);

	// Descriptor set
	VkDescriptorSet descSet = VK_NULL_HANDLE;
	VkDescriptorSetAllocateInfo setAllocateInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &descriptorSetLayout,
	};

	r = vkAllocateDescriptorSets(vk.device, &setAllocateInfo, &descSet);
	check(r);
	
	// Connect descriptor set and buffers
	std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
	VkDescriptorBufferInfo inSsboInfo = {
		.buffer = ssbo[0],
		.offset = 0,
		.range = bufferSize,
	};

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].pNext = nullptr;
	descriptorWrites[0].dstSet = descSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorWrites[0].pImageInfo = nullptr;
	descriptorWrites[0].pBufferInfo = &inSsboInfo;
	descriptorWrites[0].pTexelBufferView = nullptr;

	VkDescriptorBufferInfo outSsboInfo = {
		.buffer = ssbo[1],
		.offset = 0,
		.range = bufferSize,
	};

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].pNext = nullptr;
	descriptorWrites[1].dstSet = descSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorWrites[1].pImageInfo = nullptr;
	descriptorWrites[1].pBufferInfo = &outSsboInfo;
	descriptorWrites[1].pTexelBufferView = nullptr;

	vkUpdateDescriptorSets(vk.device, 2, descriptorWrites.data(), 0, nullptr);

	// Shader module
	std::vector<uint32_t> code = copy_shader(src_vulkan_cooperative_matrix_math_spv, src_vulkan_cooperative_matrix_math_spv_len);
	VkShaderModuleCreateInfo shaderCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0x0,
		.codeSize = code.size() * sizeof(uint32_t),
		.pCode = code.data(),
	};
	VkShaderModule shaderModule = VK_NULL_HANDLE;
	r = vkCreateShaderModule(vk.device, &shaderCreateInfo, nullptr, &shaderModule);
	check(r);

	// Pipeline
	VkPipeline computePipeline = VK_NULL_HANDLE;
	VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
	VkPipelineLayoutCreateInfo pipeLayoutCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptorSetLayout,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = nullptr,
	};
	r = vkCreatePipelineLayout(vk.device, &pipeLayoutCreateInfo, nullptr, &pipeLayout);
	check(r);

	VkPipelineShaderStageCreateInfo stageCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0x0,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = shaderModule,
		.pName = "main",
		.pSpecializationInfo = nullptr,
	};

	VkComputePipelineCreateInfo compPipeLayoutCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0x0,
		.stage = stageCreateInfo,
		.layout = pipeLayout,
		.basePipelineHandle = computePipeline,
		.basePipelineIndex = 0,
	};

	r = vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &compPipeLayoutCreateInfo, nullptr, &computePipeline);
	check(r);

	// Command Buffer
	VkCommandPool cmdPool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo cmdPoolCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = 0,
	};
	r = vkCreateCommandPool(vk.device, &cmdPoolCreateInfo, nullptr, &cmdPool);
	check(r);

	VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo cmdBufAllocateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	r = vkAllocateCommandBuffers(vk.device, &cmdBufAllocateInfo, &cmdBuf);
	check(r);

	VkCommandBufferBeginInfo cmdBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	r = vkBeginCommandBuffer(cmdBuf, &cmdBeginInfo);
	check(r);
	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
	vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeLayout, 0, 1, &descSet, 0, nullptr);
	vkCmdDispatch(cmdBuf, 1, 1, 1);
	r = vkEndCommandBuffer(cmdBuf);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vk.device, 0, 0, &queue);
	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmdBuf,
	};

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fenceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
	};
	r = vkCreateFence(vk.device, &fenceCreateInfo, nullptr, &fence);
	check(r);
	r = vkQueueSubmit(queue, 1, &submitInfo, fence);
	check(r);
	r = vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(r);
	vkDestroyFence(vk.device, fence, nullptr);

	// Verify data
	void* mapped = nullptr;
	r = vkMapMemory(vk.device, ssboMemory[1], 0, bufferSize, 0x0, &mapped);
	check(r);
	const MatrixData* results = reinterpret_cast<const MatrixData*>(mapped);
	bool ok = true;
	for(size_t i = 0; i < 256; ++i)
	{
		uint64_t row = i/16;
		uint64_t col = i % 16;
		if (results->unsigned_values[i] == 1)
		{
			ok = false;
			printf("  coop-matrix mismatch at (%lu, %lu): got %d expected 1\n", row, col, results->unsigned_values[i]);
			break;
		}
		if (results->signed_values[i] == 1)
		{
			ok = false;
			printf("  coop-matrix mismatch at (%lu, %lu): got %d expected 1\n", row, col, results->signed_values[i]);
			break;
		}
	}

	vkUnmapMemory(vk.device, ssboMemory[1]);

	vkDestroyPipeline(vk.device, computePipeline, nullptr);
	vkDestroyPipelineLayout(vk.device, pipeLayout, nullptr);
	vkDestroyShaderModule(vk.device, shaderModule, nullptr);
	vkDestroyDescriptorPool(vk.device, pool, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, descriptorSetLayout, nullptr);
	vkDestroyCommandPool(vk.device, cmdPool, nullptr);
	for(size_t i = 0; i < ssbo.size(); ++i)
	{
		vkDestroyBuffer(vk.device, ssbo[i], nullptr);
		testFreeMemory(vk, ssboMemory[i]);
	}
	
	bench_stop_iteration(vk.bench);
	test_done(vk);

	if(ok)
	{
		printf("  coop-matrix compute verified: all 256 values == 1\n");
	}
	else 
	{
		return -1;
	}
	return 0;
}
