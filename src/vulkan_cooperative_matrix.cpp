#include "vulkan_common.h"
// Shader using GL_KHR_cooperative_matrix (compiled to SPIR-V and embedded)
#include "vulkan_cooperative_matrix_comp.inc"
#include "vulkan_cooperative_matrix_m8n8k32_comp.inc"
#include "vulkan_cooperative_matrix_m8n16k16_comp.inc"
#include "vulkan_cooperative_matrix_m8n16k32_comp.inc"
#include "vulkan_cooperative_matrix_m16n8k16_comp.inc"
#include "vulkan_cooperative_matrix_m16n8k32_comp.inc"
#include "vulkan_cooperative_matrix_m16n16k16_comp.inc"
#include "vulkan_cooperative_matrix_m16n16k32_comp.inc"

#include <vector>
#include <cstdio>
#include <cmath>

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char **argv, vulkan_req_t& reqs)
{
	return false;
}

struct coop_shader_variant
{
	uint32_t m;
	uint32_t n;
	uint32_t k;
	VkComponentTypeKHR aType;
	VkComponentTypeKHR bType;
	VkComponentTypeKHR cType;
	VkComponentTypeKHR resultType;
	unsigned char* spirv;
	uint32_t spirv_len;
};

static const coop_shader_variant kVariants[] = {
	{
		8, 8, 16,
		VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR,
		VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR,
		vulkan_cooperative_matrix_comp_spirv, vulkan_cooperative_matrix_comp_spirv_len
	},
	{
		8, 8, 32,
		VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR,
		VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR,
		vulkan_cooperative_matrix_m8n8k32_comp_spirv, vulkan_cooperative_matrix_m8n8k32_comp_spirv_len
	},
	{
		8, 16, 16,
		VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR,
		VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR,
		vulkan_cooperative_matrix_m8n16k16_comp_spirv, vulkan_cooperative_matrix_m8n16k16_comp_spirv_len
	},
	{
		8, 16, 32,
		VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR,
		VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR,
		vulkan_cooperative_matrix_m8n16k32_comp_spirv, vulkan_cooperative_matrix_m8n16k32_comp_spirv_len
	},
	{
		16, 8, 16,
		VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR,
		VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR,
		vulkan_cooperative_matrix_m16n8k16_comp_spirv, vulkan_cooperative_matrix_m16n8k16_comp_spirv_len
	},
	{
		16, 8, 32,
		VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR,
		VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR,
		vulkan_cooperative_matrix_m16n8k32_comp_spirv, vulkan_cooperative_matrix_m16n8k32_comp_spirv_len
	},
	{
		16, 16, 16,
		VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR,
		VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR,
		vulkan_cooperative_matrix_m16n16k16_comp_spirv, vulkan_cooperative_matrix_m16n16k16_comp_spirv_len
	},
	{
		16, 16, 32,
		VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR,
		VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR,
		vulkan_cooperative_matrix_m16n16k32_comp_spirv, vulkan_cooperative_matrix_m16n16k32_comp_spirv_len
	},
};

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.device_extensions.push_back("VK_KHR_cooperative_matrix");
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.reqfeat12.vulkanMemoryModel = VK_TRUE;    // Enable Vulkan memory model for GL_KHR_memory_scope_semantics
	reqs.reqfeat12.shaderFloat16 = VK_TRUE;    // Half precision types are used by the shader
	VkPhysicalDeviceCooperativeMatrixFeaturesKHR coopFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR, nullptr };
	coopFeatures.cooperativeMatrix = VK_TRUE;
	coopFeatures.cooperativeMatrixRobustBufferAccess = VK_FALSE;
	coopFeatures.pNext = reqs.extension_features;
	reqs.extension_features = (VkBaseInStructure*)&coopFeatures;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	auto vk = test_init(argc, argv, "vulkan_cooperative_matrix", reqs);

	int ret = 0;

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

	std::vector<VkCooperativeMatrixPropertiesKHR> props(propCount);
	for (auto &p : props)
	{
		p = { VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR, nullptr };
	}
	if (propCount > 0)
	{
		r = pf_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(vk.physical, &propCount, props.data());
		check(r);
	}
	VkPhysicalDeviceCooperativeMatrixPropertiesKHR coopProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR, nullptr };
	VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &coopProps };
	vkGetPhysicalDeviceProperties2(vk.physical, &props2);

	printf("VK_KHR_cooperative_matrix: reported %u matrix modes\n", propCount);
	if (propCount > 0)
	{
		const auto &p = props[0];
		printf("\tfirst mode: M=%u N=%u K=%u, A=%u B=%u C=%u Result=%u, scope=0x%x\n", p.MSize, p.NSize, p.KSize, p.AType, p.BType, p.CType, p.ResultType, p.scope);
	}
	printf("\tcooperativeMatrixSupportedStages=0x%x\n", coopProps.cooperativeMatrixSupportedStages);

	if ((coopProps.cooperativeMatrixSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
	{
		printf("Skipping: cooperative matrix not supported for compute stage\n");
		bench_stop_iteration(vk.bench);
		test_done(vk);
		return 77;
	}

	const coop_shader_variant* variant = nullptr;
	for (const auto &candidate : kVariants)
	{
		for (const auto &p : props)
		{
			if (p.scope != VK_SCOPE_SUBGROUP_KHR)
				continue;
			if (p.MSize != candidate.m || p.NSize != candidate.n || p.KSize != candidate.k)
				continue;
			if (p.AType != candidate.aType || p.BType != candidate.bType)
				continue;
			if (p.CType != candidate.cType || p.ResultType != candidate.resultType)
				continue;
			variant = &candidate;
			break;
		}
		if (variant)
			break;
	}
	if (!variant)
	{
		printf("Skipping: no supported cooperative matrix mode matches available shader variants\n");
		for (const auto &p : props)
		{
			printf("\tmode: M=%u N=%u K=%u, A=%u B=%u C=%u Result=%u, scope=0x%x\n",
			       p.MSize, p.NSize, p.KSize, p.AType, p.BType, p.CType, p.ResultType, p.scope);
		}
		bench_stop_iteration(vk.bench);
		test_done(vk);
		return 77;
	}
	printf("Using cooperative matrix mode M=%u N=%u K=%u\n", variant->m, variant->n, variant->k);

	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	const VkDeviceSize buffer_size = sizeof(float) * variant->m * variant->n; // full result matrix
	VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bci.size = buffer_size;
	bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	r = vkCreateBuffer(vk.device, &bci, nullptr, &buffer);
	check(r);
	VkMemoryRequirements memreq{};
	vkGetBufferMemoryRequirements(vk.device, buffer, &memreq);
	VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	mai.allocationSize = memreq.size;
	mai.memoryTypeIndex = get_device_memory_type(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	r = vkAllocateMemory(vk.device, &mai, nullptr, &memory);
	check(r);
	r = vkBindBufferMemory(vk.device, buffer, memory, 0);
	check(r);

	VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	dslci.bindingCount = 1;
	dslci.pBindings = &binding;
	r = vkCreateDescriptorSetLayout(vk.device, &dslci, nullptr, &dsl);
	check(r);

	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSize.descriptorCount = 1;
	VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	dpci.maxSets = 1;
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes = &poolSize;
	r = vkCreateDescriptorPool(vk.device, &dpci, nullptr, &pool);
	check(r);

	VkDescriptorSet dset = VK_NULL_HANDLE;
	VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	dsai.descriptorPool = pool;
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts = &dsl;
	r = vkAllocateDescriptorSets(vk.device, &dsai, &dset);
	check(r);

	VkDescriptorBufferInfo dbi{};
	dbi.buffer = buffer;
	dbi.offset = 0;
	dbi.range = buffer_size;
	VkWriteDescriptorSet wds{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	wds.dstSet = dset;
	wds.dstBinding = 0;
	wds.descriptorCount = 1;
	wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	wds.pBufferInfo = &dbi;
	vkUpdateDescriptorSets(vk.device, 1, &wds, 0, nullptr);

	// 3) Shader module and pipeline layout/pipeline
	std::vector<uint32_t> code = copy_shader(variant->spirv, variant->spirv_len);
	VkShaderModuleCreateInfo smci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	smci.pCode = code.data();
	smci.codeSize = code.size() * sizeof(uint32_t);
	VkShaderModule module = VK_NULL_HANDLE;
	r = vkCreateShaderModule(vk.device, &smci, nullptr, &module);
	check(r);

	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &dsl;
	r = vkCreatePipelineLayout(vk.device, &plci, nullptr, &layout);
	check(r);

	VkComputePipelineCreateInfo cpci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = module;
	stage.pName = "main";
	cpci.stage = stage;
	cpci.layout = layout;
	VkPipeline pipeline = VK_NULL_HANDLE;
	r = vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline);
	check(r);

	// 4) Command buffer and submission
	VkCommandPool poolCmd = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo cpci2{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	cpci2.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cpci2.queueFamilyIndex = 0; // assume queue 0 as in other tests
	r = vkCreateCommandPool(vk.device, &cpci2, nullptr, &poolCmd);

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	cbai.commandPool = poolCmd;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;
	r = vkAllocateCommandBuffers(vk.device, &cbai, &cmd);

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(vkBeginCommandBuffer(cmd, &beginInfo));
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &dset, 0, nullptr);
	vkCmdDispatch(cmd, 1, 1, 1);
	r = vkEndCommandBuffer(cmd);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vk.device, 0, 0, &queue);
	VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;
	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	r = vkCreateFence(vk.device, &fci, nullptr, &fence);
	check(r);
	r = vkQueueSubmit(queue, 1, &si, fence);
	check(r);
	r = vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(r);
	vkDestroyFence(vk.device, fence, nullptr);

	// Read back the buffer and verify correctness
	void* mapped = nullptr;
	r = vkMapMemory(vk.device, memory, 0, buffer_size, 0, &mapped);
	check(r);
	const float* values = reinterpret_cast<const float*>(mapped);
	const float expected = static_cast<float>(variant->k);
	bool ok = true;
	for (uint32_t i = 0; i < variant->m * variant->n; ++i)
	{
		if (fabsf(values[i] - expected) > 0.001f)
		{
			printf("  coop-matrix mismatch at %u: got %f expected %f\n", i, values[i], expected);
			ok = false;
			break;
		}
	}
	vkUnmapMemory(vk.device, memory);
	if (ok)
	{
		printf("  coop-matrix compute verified: all %u values == %f\n", variant->m * variant->n, expected);
	}
	else
	{
		ret = 1;
	}

	if (vk.vkAssertBuffer)
	{
		printf("Writing out checksum\n");
		uint32_t crc = 0;
		r = vk.vkAssertBuffer(vk.device, buffer, 0, VK_WHOLE_SIZE, &crc, "Results buffer");
		check(r);
	}

	// 5) Cleanup
	vkDestroyPipeline(vk.device, pipeline, nullptr);
	vkDestroyPipelineLayout(vk.device, layout, nullptr);
	vkDestroyShaderModule(vk.device, module, nullptr);
	vkDestroyDescriptorPool(vk.device, pool, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, dsl, nullptr);
	vkDestroyCommandPool(vk.device, poolCmd, nullptr);
	vkDestroyBuffer(vk.device, buffer, nullptr);
	testFreeMemory(vk, memory);

	bench_stop_iteration(vk.bench);
	test_done(vk);

	return ret;
}
