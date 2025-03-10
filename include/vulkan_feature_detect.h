#pragma once

#include <cassert>
#include <atomic>
#include <string>
#include <unordered_set>
#include <vulkan/vulkan.h>
#include <spirv/unified1/spirv.h>

// Handle actually-used feature detection for many features during tracing. Using bool atomics here
// to make the code safe for multi-thread use. We could also store one copy of the feature lists for
// each thread and then combine them after, but this way seems simpler.
//
// We need to do this work because some developers are lazy and just pass the feature structures
// back to the driver as they received it, instead of turning on only the features they actually will
// use, making traces more non-portable than they need to be.
//
// One technique used here is that some developers may copy the feature list back to the driver but
// won't pass relevant pNext structs enabling them, so we can check if the pNext is enabled, and if
// not, just disable the feature bool. This obviously will only work when there is a pNext feature
// struct.
//
// Note that the use of extension-specific entry points is deliberately not checked here, as these
// are checked through the use of the extension mechanism instead.

struct atomicPhysicalDeviceFeatures
{
	std::atomic_bool robustBufferAccess { false }; // not handled and cannot be
	std::atomic_bool fullDrawIndexUint32 { false };
	std::atomic_bool imageCubeArray { false };
	std::atomic_bool independentBlend { false };
	std::atomic_bool geometryShader { false };
	std::atomic_bool tessellationShader { false };
	std::atomic_bool sampleRateShading { false };
	std::atomic_bool dualSrcBlend { false };
	std::atomic_bool logicOp { false };
	std::atomic_bool multiDrawIndirect { false };
	std::atomic_bool drawIndirectFirstInstance { false }; // not handled, would need to peek into possibly non-host-visible memory
	std::atomic_bool depthClamp { false };
	std::atomic_bool depthBiasClamp { false };
	std::atomic_bool fillModeNonSolid { false };
	std::atomic_bool depthBounds { false };
	std::atomic_bool wideLines { false };
	std::atomic_bool largePoints { false }; // not handled
	std::atomic_bool alphaToOne { false };
	std::atomic_bool multiViewport { false };
	std::atomic_bool samplerAnisotropy { false };
	std::atomic_bool textureCompressionETC2 { false }; // not handled
	std::atomic_bool textureCompressionASTC_LDR { false }; // not handled
	std::atomic_bool textureCompressionBC { false }; // not handled
	std::atomic_bool occlusionQueryPrecise { false };
	std::atomic_bool pipelineStatisticsQuery { false };
	std::atomic_bool vertexPipelineStoresAndAtomics { false }; // not handled
	std::atomic_bool fragmentStoresAndAtomics { false }; // not handled
	std::atomic_bool shaderTessellationAndGeometryPointSize { false }; // not handled
	std::atomic_bool shaderImageGatherExtended { false };
	std::atomic_bool shaderStorageImageExtendedFormats { false }; // not handled
	std::atomic_bool shaderStorageImageMultisample { false };
	std::atomic_bool shaderStorageImageReadWithoutFormat { false }; // not handled
	std::atomic_bool shaderStorageImageWriteWithoutFormat { false }; // not handled
	std::atomic_bool shaderUniformBufferArrayDynamicIndexing { false };
	std::atomic_bool shaderSampledImageArrayDynamicIndexing { false };
	std::atomic_bool shaderStorageBufferArrayDynamicIndexing { false };
	std::atomic_bool shaderStorageImageArrayDynamicIndexing { false };
	std::atomic_bool shaderClipDistance { false };
	std::atomic_bool shaderCullDistance { false };
	std::atomic_bool shaderFloat64 { false };
	std::atomic_bool shaderInt64 { false };
	std::atomic_bool shaderInt16 { false };
	std::atomic_bool shaderResourceResidency { false };
	std::atomic_bool shaderResourceMinLod { false };
	std::atomic_bool sparseBinding { false };
	std::atomic_bool sparseResidencyBuffer { false };
	std::atomic_bool sparseResidencyImage2D { false };
	std::atomic_bool sparseResidencyImage3D { false };
	std::atomic_bool sparseResidency2Samples { false };
	std::atomic_bool sparseResidency4Samples { false };
	std::atomic_bool sparseResidency8Samples { false };
	std::atomic_bool sparseResidency16Samples { false };
	std::atomic_bool sparseResidencyAliased { false };
	std::atomic_bool variableMultisampleRate { false }; // not handled
	std::atomic_bool inheritedQueries { false };
};

struct atomicPhysicalDeviceVulkan11Features
{
	std::atomic_bool storageBuffer16BitAccess { false };
	std::atomic_bool uniformAndStorageBuffer16BitAccess { false };
	std::atomic_bool storagePushConstant16 { false };
	std::atomic_bool storageInputOutput16 { false };
	std::atomic_bool multiview { false }; // not handled
	std::atomic_bool multiviewGeometryShader { false }; // not handled
	std::atomic_bool multiviewTessellationShader { false }; // not handled
	std::atomic_bool variablePointersStorageBuffer { false };
	std::atomic_bool variablePointers { false };
	std::atomic_bool protectedMemory { false }; // not handled
	std::atomic_bool samplerYcbcrConversion { false }; // not handled
	std::atomic_bool shaderDrawParameters { false };
};

struct atomicPhysicalDeviceVulkan12Features // most are not handled
{
	std::atomic_bool samplerMirrorClampToEdge { false };
	std::atomic_bool drawIndirectCount { false };
	std::atomic_bool storageBuffer8BitAccess { false };
	std::atomic_bool uniformAndStorageBuffer8BitAccess { false };
	std::atomic_bool storagePushConstant8 { false };
	std::atomic_bool shaderBufferInt64Atomics { false };
	std::atomic_bool shaderSharedInt64Atomics { false };
	std::atomic_bool shaderFloat16 { false };
	std::atomic_bool shaderInt8 { false };
	std::atomic_bool descriptorIndexing { false };
	std::atomic_bool shaderInputAttachmentArrayDynamicIndexing { false };
	std::atomic_bool shaderUniformTexelBufferArrayDynamicIndexing { false };
	std::atomic_bool shaderStorageTexelBufferArrayDynamicIndexing { false };
	std::atomic_bool shaderUniformBufferArrayNonUniformIndexing { false };
	std::atomic_bool shaderSampledImageArrayNonUniformIndexing { false };
	std::atomic_bool shaderStorageBufferArrayNonUniformIndexing { false };
	std::atomic_bool shaderStorageImageArrayNonUniformIndexing { false };
	std::atomic_bool shaderInputAttachmentArrayNonUniformIndexing { false };
	std::atomic_bool shaderUniformTexelBufferArrayNonUniformIndexing { false };
	std::atomic_bool shaderStorageTexelBufferArrayNonUniformIndexing { false };
	std::atomic_bool descriptorBindingUniformBufferUpdateAfterBind { false };
	std::atomic_bool descriptorBindingSampledImageUpdateAfterBind { false };
	std::atomic_bool descriptorBindingStorageImageUpdateAfterBind { false };
	std::atomic_bool descriptorBindingStorageBufferUpdateAfterBind { false };
	std::atomic_bool descriptorBindingUniformTexelBufferUpdateAfterBind { false };
	std::atomic_bool descriptorBindingStorageTexelBufferUpdateAfterBind { false };
	std::atomic_bool descriptorBindingUpdateUnusedWhilePending { false };
	std::atomic_bool descriptorBindingPartiallyBound { false };
	std::atomic_bool descriptorBindingVariableDescriptorCount { false };
	std::atomic_bool runtimeDescriptorArray { false };
	std::atomic_bool samplerFilterMinmax { false };
	std::atomic_bool scalarBlockLayout { false };
	std::atomic_bool imagelessFramebuffer { false };
	std::atomic_bool uniformBufferStandardLayout { false };
	std::atomic_bool shaderSubgroupExtendedTypes { false };
	std::atomic_bool separateDepthStencilLayouts { false };
	std::atomic_bool hostQueryReset { false };
	std::atomic_bool timelineSemaphore { false };
	std::atomic_bool bufferDeviceAddress { false };
	std::atomic_bool bufferDeviceAddressCaptureReplay { false };
	std::atomic_bool bufferDeviceAddressMultiDevice { false };
	std::atomic_bool vulkanMemoryModel { false };
	std::atomic_bool vulkanMemoryModelDeviceScope { false };
	std::atomic_bool vulkanMemoryModelAvailabilityVisibilityChains { false };
	std::atomic_bool shaderOutputViewportIndex { false };
	std::atomic_bool shaderOutputLayer { false };
	std::atomic_bool subgroupBroadcastDynamicId { false };
};

struct atomicPhysicalDeviceVulkan13Features // most are not handled
{
	std::atomic_bool robustImageAccess { false };
	std::atomic_bool inlineUniformBlock { false };
	std::atomic_bool descriptorBindingInlineUniformBlockUpdateAfterBind { false };
	std::atomic_bool pipelineCreationCacheControl { false };
	std::atomic_bool privateData { false };
	std::atomic_bool shaderDemoteToHelperInvocation { false };
	std::atomic_bool shaderTerminateInvocation { false };
	std::atomic_bool subgroupSizeControl { false };
	std::atomic_bool computeFullSubgroups { false };
	std::atomic_bool synchronization2 { false };
	std::atomic_bool textureCompressionASTC_HDR { false };
	std::atomic_bool shaderZeroInitializeWorkgroupMemory { false };
	std::atomic_bool dynamicRendering { false };
	std::atomic_bool shaderIntegerDotProduct { false };
	std::atomic_bool maintenance4 { false };
};

static __attribute__((pure)) inline const void* get_extension(const void* sptr, VkStructureType sType)
{
	const VkBaseOutStructure* ptr = (VkBaseOutStructure*)sptr;
	while (ptr != nullptr && ptr->sType != sType) ptr = ptr->pNext;
	return ptr;
}

static inline void prune_extension(void* sptr, VkStructureType sType)
{
	VkBaseOutStructure* ptr = (VkBaseOutStructure*)sptr;
	VkBaseOutStructure* prev = nullptr;
	while (ptr != nullptr && ptr->sType != sType) { prev = ptr; ptr = ptr->pNext; }
	if (prev && ptr && ptr->sType == sType) prev->pNext = ptr->pNext;
}

struct feature_detection
{
	// Tracking data
	struct atomicPhysicalDeviceFeatures core10;
	struct atomicPhysicalDeviceVulkan11Features core11;
	struct atomicPhysicalDeviceVulkan12Features core12;
	struct atomicPhysicalDeviceVulkan13Features core13;
	std::atomic_bool has_VK_EXT_swapchain_colorspace { false };
	std::atomic_bool has_VkPhysicalDeviceShaderAtomicInt64Features { false };
	std::atomic_bool has_VK_KHR_shared_presentable_image { false };
	std::atomic_bool has_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT { false };

	// --- Utility functions ---

	inline bool is_colorspace_ext(VkColorSpaceKHR s)
	{
		return (s == VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT || s == VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT
		        || s == VK_COLOR_SPACE_BT2020_LINEAR_EXT || s == VK_COLOR_SPACE_BT709_LINEAR_EXT
		        || s == VK_COLOR_SPACE_BT709_NONLINEAR_EXT || s == VK_COLOR_SPACE_DCI_P3_LINEAR_EXT
		        || s == VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT || s == VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT
		        || s == VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT || s == VK_COLOR_SPACE_DOLBYVISION_EXT
		        || s == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT || s == VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT
		        || s == VK_COLOR_SPACE_HDR10_HLG_EXT || s == VK_COLOR_SPACE_HDR10_ST2084_EXT || s == VK_COLOR_SPACE_PASS_THROUGH_EXT);
	}

	// --- SPIRV handling ---

	void parse_SPIRV(const uint32_t* code, uint32_t code_size)
	{
		uint16_t opcode;
		uint16_t word_count;
		const uint32_t* insn = code + 5;
		code_size /= 4; // bytes to words
		do {
			opcode = uint16_t(insn[0]);
			word_count = uint16_t(insn[0] >> 16);
			if (opcode == SpvOpCapability)
			{
				switch (insn[1])
				{
				case SpvCapabilityImageGatherExtended: core10.shaderImageGatherExtended = true; break;
				case SpvCapabilityUniformBufferArrayDynamicIndexing: core10.shaderUniformBufferArrayDynamicIndexing = true; break;
				case SpvCapabilitySampledImageArrayDynamicIndexing: core10.shaderSampledImageArrayDynamicIndexing = true; break;
				case SpvCapabilityStorageBufferArrayDynamicIndexing: core10.shaderStorageBufferArrayDynamicIndexing = true; break;
				case SpvCapabilityStorageImageArrayDynamicIndexing: core10.shaderStorageImageArrayDynamicIndexing = true; break;
				case SpvCapabilityClipDistance: core10.shaderClipDistance = true; break;
				case SpvCapabilityCullDistance: core10.shaderCullDistance = true; break;
				case SpvCapabilityFloat64: core10.shaderFloat64 = true; break;
				case SpvCapabilityInt64: core10.shaderInt64 = true; break;
				case SpvCapabilityInt16: core10.shaderInt16 = true; break;
				case SpvCapabilityMinLod: core10.shaderResourceMinLod = true; break;
				case SpvCapabilitySampledCubeArray: core10.imageCubeArray = true; break;
				case SpvCapabilityImageCubeArray: core10.imageCubeArray = true; break;
				case SpvCapabilitySparseResidency: core10.shaderResourceResidency = true; break;
				case SpvCapabilityStorageBuffer16BitAccess: core11.storageBuffer16BitAccess = true; break;
				case SpvCapabilityUniformAndStorageBuffer16BitAccess: core11.uniformAndStorageBuffer16BitAccess = true; break;
				case SpvCapabilityStoragePushConstant16: core11.storagePushConstant16 = true; break;
				case SpvCapabilityStorageInputOutput16: core11.storageInputOutput16 = true; break;
				case SpvCapabilityVariablePointersStorageBuffer: core11.variablePointersStorageBuffer = true; break;
				case SpvCapabilityVariablePointers: core11.variablePointers = true; break;
				case SpvCapabilityDrawParameters: core11.shaderDrawParameters = true; break;
				default: break;
				}
			}
			insn += word_count;
		}
		while (insn != code + code_size && opcode != SpvOpMemoryModel);
	}

	// --- Checking structures Call these for all these structures after they are successfully used. ---

	void check_VkGraphicsPipelineCreateInfo(const VkGraphicsPipelineCreateInfo* info)
	{
		if (info->pRasterizationState && info->pRasterizationState->depthBiasClamp != 0.0) core10.depthBiasClamp = true;
		if (info->pRasterizationState && info->pRasterizationState->lineWidth != 1.0) core10.wideLines = true;
	}

	void check_VkDeviceCreateInfo(const VkDeviceCreateInfo* info)
	{
		VkPhysicalDeviceShaderAtomicInt64Features* pdsai64f = (VkPhysicalDeviceShaderAtomicInt64Features*)get_extension(info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES);
		if (pdsai64f && (pdsai64f->shaderBufferInt64Atomics || pdsai64f->shaderSharedInt64Atomics)) has_VkPhysicalDeviceShaderAtomicInt64Features = true;

		VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT* pdsiai64f = (VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT*)get_extension(info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT);
		if (pdsiai64f && (pdsiai64f->shaderImageInt64Atomics || pdsiai64f->sparseImageInt64Atomics)) has_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT = true;
	}

	void check_VkSurfaceCapabilities2KHR(const VkSurfaceCapabilities2KHR* info)
	{
		VkSharedPresentSurfaceCapabilitiesKHR* sc = (VkSharedPresentSurfaceCapabilitiesKHR*)get_extension(info, VK_STRUCTURE_TYPE_SHARED_PRESENT_SURFACE_CAPABILITIES_KHR);
		if (sc && sc->sharedPresentSupportedUsageFlags) has_VK_KHR_shared_presentable_image = true;
	}

	void check_VkSwapchainCreateInfoKHR(const VkSwapchainCreateInfoKHR* info)
	{
		if (is_colorspace_ext(info->imageColorSpace)) has_VK_EXT_swapchain_colorspace = true;
	}

	void check_VkPipelineShaderStageCreateInfo(const VkPipelineShaderStageCreateInfo* info)
	{
		if (info->stage == VK_SHADER_STAGE_GEOMETRY_BIT) core10.geometryShader = true;
		else if (info->stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || info->stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) core10.tessellationShader = true;
	}

	void check_VkPipelineColorBlendAttachmentState(const VkPipelineColorBlendAttachmentState* info)
	{
		const VkBlendFactor factors[4] = { VK_BLEND_FACTOR_SRC1_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR, VK_BLEND_FACTOR_SRC1_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA };
		for (int i = 0; i < 4; i++) if (info->srcColorBlendFactor == factors[i]) core10.dualSrcBlend = true;
		for (int i = 0; i < 4; i++) if (info->dstColorBlendFactor == factors[i]) core10.dualSrcBlend = true;
		for (int i = 0; i < 4; i++) if (info->srcAlphaBlendFactor == factors[i]) core10.dualSrcBlend = true;
		for (int i = 0; i < 4; i++) if (info->dstAlphaBlendFactor == factors[i]) core10.dualSrcBlend = true;
	}

	void check_VkSamplerCreateInfo(const VkSamplerCreateInfo* info)
	{
		if (info->anisotropyEnable == VK_TRUE) core10.samplerAnisotropy = true;
		if (info->addressModeU == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
		    || info->addressModeV == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
		    || info->addressModeW == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE)
			core12.samplerMirrorClampToEdge = true;
	}

	void check_VkQueryPoolCreateInfo(const VkQueryPoolCreateInfo* info)
	{
		if (info->queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS && info->pipelineStatistics != 0) core10.pipelineStatisticsQuery = true;
	}

	void check_VkPipelineColorBlendStateCreateInfo(const VkPipelineColorBlendStateCreateInfo* info)
	{
		if (info->logicOpEnable == VK_TRUE) core10.logicOp = true;

		if (info->attachmentCount > 1)
		{
			for (uint32_t i = 1; i < info->attachmentCount; i++)
			{
				if (info->pAttachments[i].blendEnable         != info->pAttachments[i - 1].blendEnable ||
					info->pAttachments[i].srcColorBlendFactor != info->pAttachments[i - 1].srcColorBlendFactor ||
					info->pAttachments[i].dstColorBlendFactor != info->pAttachments[i - 1].dstColorBlendFactor ||
					info->pAttachments[i].colorBlendOp        != info->pAttachments[i - 1].colorBlendOp ||
					info->pAttachments[i].srcAlphaBlendFactor != info->pAttachments[i - 1].srcAlphaBlendFactor ||
					info->pAttachments[i].dstAlphaBlendFactor != info->pAttachments[i - 1].dstAlphaBlendFactor ||
					info->pAttachments[i].alphaBlendOp        != info->pAttachments[i - 1].alphaBlendOp ||
					info->pAttachments[i].colorWriteMask      != info->pAttachments[i - 1].colorWriteMask)
				{
					core10.independentBlend = true;
				}
			}
		}
	}

	void check_VkPipelineMultisampleStateCreateInfo(const VkPipelineMultisampleStateCreateInfo* info)
	{
		if (info->alphaToOneEnable == VK_TRUE) core10.alphaToOne = true;
		if (info->sampleShadingEnable == VK_TRUE) core10.sampleRateShading = true;
	}

	void check_VkImageCreateInfo(const VkImageCreateInfo* info)
	{
		if (info->imageType == VK_IMAGE_TYPE_2D && info->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)
		{
			if (info->samples == VK_SAMPLE_COUNT_1_BIT) core10.sparseResidencyImage2D = true;
			else if (info->samples == VK_SAMPLE_COUNT_2_BIT) core10.sparseResidency2Samples = true;
			else if (info->samples == VK_SAMPLE_COUNT_4_BIT) core10.sparseResidency4Samples = true;
			else if (info->samples == VK_SAMPLE_COUNT_8_BIT) core10.sparseResidency8Samples = true;
			else if (info->samples == VK_SAMPLE_COUNT_16_BIT) core10.sparseResidency16Samples = true;
		}
		else if (info->imageType == VK_IMAGE_TYPE_3D && info->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) core10.sparseResidencyImage3D = true;

		if (info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) core10.sparseBinding = true;
		if (info->flags & VK_IMAGE_CREATE_SPARSE_ALIASED_BIT) core10.sparseResidencyAliased = true;
		if ((info->usage & VK_IMAGE_USAGE_STORAGE_BIT) && info->samples != VK_SAMPLE_COUNT_1_BIT) core10.shaderStorageImageMultisample = true;
	}

	void check_VkBufferCreateInfo(const VkBufferCreateInfo* info)
	{
		if (info->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
		{
			core10.sparseBinding = true;
		}
		if (info->flags & VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT)
		{
			core10.sparseResidencyBuffer = true;
		}
		if (info->flags & VK_BUFFER_CREATE_SPARSE_ALIASED_BIT) core10.sparseResidencyAliased = true;
	}

	void check_VkPipelineRasterizationStateCreateInfo(const VkPipelineRasterizationStateCreateInfo* info)
	{
		if (info->depthClampEnable == VK_TRUE) core10.depthClamp = true;
		if (info->polygonMode == VK_POLYGON_MODE_POINT || info->polygonMode == VK_POLYGON_MODE_LINE) core10.fillModeNonSolid = true;
	}

	void check_VkPipelineDepthStencilStateCreateInfo(const VkPipelineDepthStencilStateCreateInfo* info)
	{
		if (info->depthBoundsTestEnable == VK_TRUE) core10.depthBounds = true;
	}

	void check_VkImageViewCreateInfo(const VkImageViewCreateInfo* info)
	{
		if (info->viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) core10.imageCubeArray = true;
	}

	void check_VkCommandBufferInheritanceInfo(const VkCommandBufferInheritanceInfo* info)
	{
		if (info->occlusionQueryEnable != VK_FALSE || ((info->queryFlags & ~(VK_QUERY_CONTROL_PRECISE_BIT)) == 0))
		{
			core10.inheritedQueries = true;
		}
	}

	void check_VkPipelineViewportStateCreateInfo(const VkPipelineViewportStateCreateInfo* info)
	{
		if (info->viewportCount > 1 || info->scissorCount > 1)
		{
			core10.multiViewport = true;
		}
	}

	void check_VkPipelineViewportExclusiveScissorStateCreateInfoNV(const VkPipelineViewportExclusiveScissorStateCreateInfoNV* info)
	{
		if (info->exclusiveScissorCount != 0 && info->exclusiveScissorCount != 1)
		{
			core10.multiViewport = true;
		}
	}

	// --- Checking functions. Call these for all these Vulkan commands after they are successfully called, before returning. ---

	void check_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
	{
		if (lineWidth != 1.0) core10.wideLines = true;
	}

	void check_vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
	{
		if (depthBiasClamp != 0.0) core10.depthBiasClamp = true;
	}

	void check_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
	{
		if (drawCount > 1) core10.multiDrawIndirect = true;
	}

	void check_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
	{
		if (drawCount > 1) core10.multiDrawIndirect = true;
	}

	void check_vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
	{
		if (flags & VK_QUERY_CONTROL_PRECISE_BIT) core10.occlusionQueryPrecise = true;
	}

	void check_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
	{
		core12.drawIndirectCount = true;
	}

	void check_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
	{
		if (indexType == VK_INDEX_TYPE_UINT32) core10.fullDrawIndexUint32 = true; // defensive assumption
	}

	void check_vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType)
	{
		if (indexType == VK_INDEX_TYPE_UINT32) core10.fullDrawIndexUint32 = true; // defensive assumptiom
	}

	void check_vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
	{
		core12.drawIndirectCount = true;
	}

	void check_vkResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
	{
		core12.hostQueryReset = true;
	}

	void check_vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo)
	{
		core13.dynamicRendering = true;
	}

	void check_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports)
	{
		if (firstViewport != 0 || viewportCount != 1)
		{
			core10.multiViewport = true;
		}
	}

	void check_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors)
	{
		if (firstScissor != 0 || scissorCount != 1)
		{
			core10.multiViewport = true;
		}
	}

	void check_vkCmdSetExclusiveScissorNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkRect2D* pExclusiveScissors)
	{
		if (firstExclusiveScissor != 0 || exclusiveScissorCount != 1)
		{
			core10.multiViewport = true;
		}
	}

	// --- Remove unused feature bits from these structures ---

	void adjust_VkDeviceCreateInfo(VkDeviceCreateInfo* info, const std::unordered_set<std::string>& exts)
	{
		if (exts.count("VK_KHR_shader_atomic_int64") == 0) prune_extension(info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES);
		if (exts.count("VK_EXT_shader_image_atomic_int64") == 0) prune_extension(info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT);
	}

	void adjust_VkInstanceCreateInfo(VkInstanceCreateInfo* info, const std::unordered_set<std::string>& exts)
	{
	}

	std::unordered_set<std::string> adjust_device_extensions(std::unordered_set<std::string>& exts)
	{
		std::unordered_set<std::string> removed;
		if (!has_VkPhysicalDeviceShaderAtomicInt64Features) removed.insert(exts.extract("VK_KHR_shader_atomic_int64"));
		if (!has_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT) removed.insert(exts.extract("VK_EXT_shader_image_atomic_int64")); // alias of above
		if (!has_VK_KHR_shared_presentable_image) removed.insert(exts.extract("VK_KHR_shared_presentable_image"));
		return removed;
	}

	std::unordered_set<std::string> adjust_instance_extensions(std::unordered_set<std::string>& exts)
	{
		std::unordered_set<std::string> removed;
		if (!has_VK_EXT_swapchain_colorspace) removed.insert(exts.extract("VK_EXT_swapchain_colorspace"));
		return removed;
	}

	void adjust_VkPhysicalDeviceFeatures(VkPhysicalDeviceFeatures& incore10)
	{
		// Only turn off the features we have checking code for
		#define CHECK_FEATURE10(_x) if (!core10._x) incore10._x = false;
		CHECK_FEATURE10(fullDrawIndexUint32);
		CHECK_FEATURE10(dualSrcBlend);
		CHECK_FEATURE10(geometryShader);
		CHECK_FEATURE10(tessellationShader);
		CHECK_FEATURE10(sampleRateShading);
		CHECK_FEATURE10(depthClamp);
		CHECK_FEATURE10(depthBiasClamp);
		CHECK_FEATURE10(wideLines);
		CHECK_FEATURE10(samplerAnisotropy);
		CHECK_FEATURE10(fillModeNonSolid);
		CHECK_FEATURE10(depthBounds);
		CHECK_FEATURE10(pipelineStatisticsQuery);
		CHECK_FEATURE10(shaderStorageImageMultisample);
		CHECK_FEATURE10(logicOp);
		CHECK_FEATURE10(alphaToOne);
		CHECK_FEATURE10(sparseBinding);
		CHECK_FEATURE10(sparseResidencyBuffer);
		CHECK_FEATURE10(sparseResidencyImage2D);
		CHECK_FEATURE10(sparseResidencyImage3D);
		CHECK_FEATURE10(sparseResidency2Samples);
		CHECK_FEATURE10(sparseResidency4Samples);
		CHECK_FEATURE10(sparseResidency8Samples);
		CHECK_FEATURE10(sparseResidency16Samples);
		CHECK_FEATURE10(sparseResidencyAliased);
		CHECK_FEATURE10(independentBlend);
		CHECK_FEATURE10(inheritedQueries);
		CHECK_FEATURE10(multiViewport);
		CHECK_FEATURE10(imageCubeArray);
		CHECK_FEATURE10(shaderImageGatherExtended);
		CHECK_FEATURE10(shaderUniformBufferArrayDynamicIndexing);
		CHECK_FEATURE10(shaderSampledImageArrayDynamicIndexing);
		CHECK_FEATURE10(shaderStorageBufferArrayDynamicIndexing);
		CHECK_FEATURE10(shaderStorageImageArrayDynamicIndexing);
		CHECK_FEATURE10(shaderClipDistance);
		CHECK_FEATURE10(shaderCullDistance);
		CHECK_FEATURE10(shaderFloat64);
		CHECK_FEATURE10(shaderInt64);
		CHECK_FEATURE10(shaderInt16);
		CHECK_FEATURE10(shaderResourceMinLod);
		CHECK_FEATURE10(shaderResourceResidency);
		CHECK_FEATURE10(multiDrawIndirect);
		CHECK_FEATURE10(occlusionQueryPrecise);
		#undef CHECK_FEATURE10
	}

	void adjust_VkPhysicalDeviceVulkan11Features(VkPhysicalDeviceVulkan11Features& incore11)
	{
		// Only turn off the features we have checking code for
		#define CHECK_FEATURE11(_x) if (!core11._x) incore11._x = false;
		CHECK_FEATURE11(storageBuffer16BitAccess);
		CHECK_FEATURE11(uniformAndStorageBuffer16BitAccess);
		CHECK_FEATURE11(storagePushConstant16);
		CHECK_FEATURE11(storageInputOutput16);
		CHECK_FEATURE11(variablePointersStorageBuffer);
		CHECK_FEATURE11(variablePointers);
		CHECK_FEATURE11(shaderDrawParameters);
		#undef CHECK_FEATURE11
	}

	void adjust_VkPhysicalDeviceVulkan12Features(VkPhysicalDeviceVulkan12Features& incore12)
	{
		// Only turn off the features we have checking code for
		#define CHECK_FEATURE12(_x) if (!core12._x) incore12._x = false;
		CHECK_FEATURE12(drawIndirectCount);
		CHECK_FEATURE12(hostQueryReset);
		CHECK_FEATURE12(samplerMirrorClampToEdge);
		#undef CHECK_FEATURE12
	}

	void adjust_VkPhysicalDeviceVulkan13Features(VkPhysicalDeviceVulkan13Features& incore13)
	{
		// Only turn off the features we have checking code for
		#define CHECK_FEATURE13(_x) if (!core13._x) incore13._x = false;
		CHECK_FEATURE13(dynamicRendering);
		#undef CHECK_FEATURE13
	}
};
