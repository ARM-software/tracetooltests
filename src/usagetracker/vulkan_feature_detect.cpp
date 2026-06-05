#include <cassert>
#include <atomic>
#include <string>
#include <unordered_set>
#include <vector>
#include "spirv/unified1/spirv.h"
#include "vulkan/vulkan.h"

#include "vulkan_feature_detect.h"

static feature_detection* instance = nullptr;

feature_detection* vulkan_feature_detection_get()
{
	if (!instance) instance = new feature_detection;
	return instance;
}

void vulkan_feature_detection_reset()
{
	delete instance;
	instance = new feature_detection;
}

// --- Utility functions ---

static __attribute__((pure)) inline const void* get_extension(const void* sptr, VkStructureType sType)
{
	const VkBaseOutStructure* ptr = (VkBaseOutStructure*)sptr;
	while (ptr != nullptr && ptr->sType != sType) ptr = ptr->pNext;
	return ptr;
}

static inline bool prune_extension(void* sptr, VkStructureType sType)
{
	VkBaseOutStructure* ptr = (VkBaseOutStructure*)sptr;
	VkBaseOutStructure* prev = nullptr;
	while (ptr != nullptr && ptr->sType != sType) { prev = ptr; ptr = ptr->pNext; }
	if (prev && ptr && ptr->sType == sType) { prev->pNext = ptr->pNext; return true; }
	return false;
}

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

inline bool is_ray_tracing_maintenance1_query_type(VkQueryType type)
{
	return type == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR ||
	       type == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR;
}

inline bool uses_ray_tracing_maintenance1_stage(VkPipelineStageFlags2 stages)
{
	return (stages & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR) != 0;
}

inline bool uses_ray_tracing_maintenance1_access(VkAccessFlags2 access)
{
	return (access & VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR) != 0;
}

inline bool is_opacity_micromap_query_type(VkQueryType type)
{
	return type == VK_QUERY_TYPE_MICROMAP_SERIALIZATION_SIZE_EXT ||
	       type == VK_QUERY_TYPE_MICROMAP_COMPACTED_SIZE_EXT;
}

inline bool uses_opacity_micromap_stage(VkPipelineStageFlags2 stages)
{
	return (stages & VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT) != 0;
}

inline bool uses_opacity_micromap_access(VkAccessFlags2 access)
{
	return (access & (VK_ACCESS_2_MICROMAP_READ_BIT_EXT | VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT)) != 0;
}

static bool pipeline_flags2_uses_pipeline_opacity_micromap(const void* pNext)
{
	const VkPipelineCreateFlags2CreateInfo* flags2 =
		(const VkPipelineCreateFlags2CreateInfo*)get_extension(pNext, VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO);
	return flags2 && (flags2->flags & VK_PIPELINE_CREATE_2_DISALLOW_OPACITY_MICROMAP_BIT_ARM) != 0;
}

inline bool is_ray_tracing_maintenance1_token_type(VkIndirectCommandsTokenTypeEXT type)
{
	return type == VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT;
}

static bool build_info_uses_opacity_micromap(const VkAccelerationStructureBuildGeometryInfoKHR* info)
{
	if (!info) return false;
	assert(info->geometryCount == 0 || info->pGeometries != nullptr || info->ppGeometries != nullptr);

	for (uint32_t i = 0; i < info->geometryCount; i++)
	{
		const VkAccelerationStructureGeometryKHR* geometry =
			info->pGeometries ? &info->pGeometries[i] : info->ppGeometries[i];
		assert(geometry != nullptr);
		if (geometry->geometryType != VK_GEOMETRY_TYPE_TRIANGLES_KHR) continue;
		if (get_extension(geometry->geometry.triangles.pNext, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT))
			return true;
	}

	return (info->flags & (VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_BIT_EXT |
	                       VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DISABLE_OPACITY_MICROMAPS_BIT_EXT |
	                       VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_DATA_UPDATE_BIT_EXT)) != 0;
}

static bool dependency_info_uses_ray_tracing_maintenance1(const VkDependencyInfo* info)
{
	if (!info) return false;
	assert(info->memoryBarrierCount == 0 || info->pMemoryBarriers != nullptr);
	assert(info->bufferMemoryBarrierCount == 0 || info->pBufferMemoryBarriers != nullptr);
	assert(info->imageMemoryBarrierCount == 0 || info->pImageMemoryBarriers != nullptr);

	for (uint32_t i = 0; i < info->memoryBarrierCount; i++)
	{
		const VkMemoryBarrier2& barrier = info->pMemoryBarriers[i];
		if (uses_ray_tracing_maintenance1_stage(barrier.srcStageMask) || uses_ray_tracing_maintenance1_stage(barrier.dstStageMask) ||
		    uses_ray_tracing_maintenance1_access(barrier.srcAccessMask) || uses_ray_tracing_maintenance1_access(barrier.dstAccessMask))
			return true;
	}

	for (uint32_t i = 0; i < info->bufferMemoryBarrierCount; i++)
	{
		const VkBufferMemoryBarrier2& barrier = info->pBufferMemoryBarriers[i];
		if (uses_ray_tracing_maintenance1_stage(barrier.srcStageMask) || uses_ray_tracing_maintenance1_stage(barrier.dstStageMask) ||
		    uses_ray_tracing_maintenance1_access(barrier.srcAccessMask) || uses_ray_tracing_maintenance1_access(barrier.dstAccessMask))
			return true;
	}

	for (uint32_t i = 0; i < info->imageMemoryBarrierCount; i++)
	{
		const VkImageMemoryBarrier2& barrier = info->pImageMemoryBarriers[i];
		if (uses_ray_tracing_maintenance1_stage(barrier.srcStageMask) || uses_ray_tracing_maintenance1_stage(barrier.dstStageMask) ||
		    uses_ray_tracing_maintenance1_access(barrier.srcAccessMask) || uses_ray_tracing_maintenance1_access(barrier.dstAccessMask))
			return true;
	}

	return false;
}

static bool dependency_info_uses_opacity_micromap(const VkDependencyInfo* info)
{
	if (!info) return false;
	assert(info->memoryBarrierCount == 0 || info->pMemoryBarriers != nullptr);
	assert(info->bufferMemoryBarrierCount == 0 || info->pBufferMemoryBarriers != nullptr);
	assert(info->imageMemoryBarrierCount == 0 || info->pImageMemoryBarriers != nullptr);

	for (uint32_t i = 0; i < info->memoryBarrierCount; i++)
	{
		const VkMemoryBarrier2& barrier = info->pMemoryBarriers[i];
		if (uses_opacity_micromap_stage(barrier.srcStageMask) || uses_opacity_micromap_stage(barrier.dstStageMask) ||
		    uses_opacity_micromap_access(barrier.srcAccessMask) || uses_opacity_micromap_access(barrier.dstAccessMask))
			return true;
	}

	for (uint32_t i = 0; i < info->bufferMemoryBarrierCount; i++)
	{
		const VkBufferMemoryBarrier2& barrier = info->pBufferMemoryBarriers[i];
		if (uses_opacity_micromap_stage(barrier.srcStageMask) || uses_opacity_micromap_stage(barrier.dstStageMask) ||
		    uses_opacity_micromap_access(barrier.srcAccessMask) || uses_opacity_micromap_access(barrier.dstAccessMask))
			return true;
	}

	for (uint32_t i = 0; i < info->imageMemoryBarrierCount; i++)
	{
		const VkImageMemoryBarrier2& barrier = info->pImageMemoryBarriers[i];
		if (uses_opacity_micromap_stage(barrier.srcStageMask) || uses_opacity_micromap_stage(barrier.dstStageMask) ||
		    uses_opacity_micromap_access(barrier.srcAccessMask) || uses_opacity_micromap_access(barrier.dstAccessMask))
			return true;
	}

	return false;
}

static bool indirect_commands_layout_uses_ray_tracing_maintenance1(const VkIndirectCommandsLayoutCreateInfoEXT* info)
{
	if (!info) return false;
	assert(info->tokenCount == 0 || info->pTokens != nullptr);

	for (uint32_t i = 0; i < info->tokenCount; i++)
	{
		if (is_ray_tracing_maintenance1_token_type(info->pTokens[i].type)) return true;
	}

	return false;
}

static bool submit_info_uses_ray_tracing_maintenance1(const VkSubmitInfo2* info)
{
	if (!info) return false;
	assert(info->waitSemaphoreInfoCount == 0 || info->pWaitSemaphoreInfos != nullptr);
	assert(info->signalSemaphoreInfoCount == 0 || info->pSignalSemaphoreInfos != nullptr);

	for (uint32_t i = 0; i < info->waitSemaphoreInfoCount; i++)
	{
		if (uses_ray_tracing_maintenance1_stage(info->pWaitSemaphoreInfos[i].stageMask)) return true;
	}

	for (uint32_t i = 0; i < info->signalSemaphoreInfoCount; i++)
	{
		if (uses_ray_tracing_maintenance1_stage(info->pSignalSemaphoreInfos[i].stageMask)) return true;
	}

	return false;
}

static bool uses_pre13_dynamic_rendering()
{
	return instance->requested_instance_api_version.load() < VK_API_VERSION_1_3;
}

static bool uses_pre13_synchronization2()
{
	return instance->requested_instance_api_version.load() < VK_API_VERSION_1_3;
}

static bool has_enabled_tensor_features(const VkPhysicalDeviceTensorFeaturesARM* features)
{
	if (!features) return false;
	return features->tensorNonPacked || features->shaderTensorAccess ||
	       features->shaderStorageTensorArrayDynamicIndexing ||
	       features->shaderStorageTensorArrayNonUniformIndexing ||
	       features->descriptorBindingStorageTensorUpdateAfterBind ||
	       features->tensors;
}

static bool dependency_info_uses_tensors(const VkDependencyInfo* info)
{
	if (!info) return false;
	const VkTensorDependencyInfoARM* tensor_info =
		(const VkTensorDependencyInfoARM*)get_extension(info, VK_STRUCTURE_TYPE_TENSOR_DEPENDENCY_INFO_ARM);
	return tensor_info && tensor_info->tensorMemoryBarrierCount > 0;
}

static bool submit_pnext_uses_tensors(const void* pNext)
{
	return get_extension(pNext, VK_STRUCTURE_TYPE_FRAME_BOUNDARY_TENSORS_ARM) != nullptr;
}

static bool array_has_nonzero(const uint32_t* values, uint32_t count)
{
	if (count == 0) return false;
	assert(values != nullptr);
	for (uint32_t i = 0; i < count; i++) if (values[i] != 0) return true;
	return false;
}

static bool array_has_nonzero(const int32_t* values, uint32_t count)
{
	if (count == 0) return false;
	assert(values != nullptr);
	for (uint32_t i = 0; i < count; i++) if (values[i] != 0) return true;
	return false;
}

static bool is_bc_format(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC2_SRGB_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_BC4_UNORM_BLOCK:
	case VK_FORMAT_BC4_SNORM_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
	case VK_FORMAT_BC5_SNORM_BLOCK:
	case VK_FORMAT_BC6H_UFLOAT_BLOCK:
	case VK_FORMAT_BC6H_SFLOAT_BLOCK:
	case VK_FORMAT_BC7_UNORM_BLOCK:
	case VK_FORMAT_BC7_SRGB_BLOCK:
		return true;
	default:
		return false;
	}
}

static bool is_etc2_format(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
	case VK_FORMAT_EAC_R11_UNORM_BLOCK:
	case VK_FORMAT_EAC_R11_SNORM_BLOCK:
	case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
	case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
		return true;
	default:
		return false;
	}
}

static bool is_astc_ldr_format(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
	case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
	case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
	case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
	case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
	case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
	case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
	case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
	case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
	case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
	case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
	case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
	case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
	case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
	case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
	case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
		return true;
	default:
		return false;
	}
}

static void mark_external_memory_usage(VkExternalMemoryHandleTypeFlags handleTypes)
{
	if (handleTypes != 0) instance->has_VK_KHR_external_memory = true;
	if (handleTypes & (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT))
	{
		instance->has_VK_KHR_external_memory_fd = true;
	}
}

static void mark_all_stippled_line_features_used()
{
	instance->core14.stippledRectangularLines = true;
	instance->core14.stippledBresenhamLines = true;
	instance->core14.stippledSmoothLines = true;
}

static void mark_line_rasterization_mode_usage(VkLineRasterizationMode mode, VkBool32 stippledLineEnable)
{
	switch (mode)
	{
	case VK_LINE_RASTERIZATION_MODE_DEFAULT:
		if (stippledLineEnable == VK_TRUE) instance->core14.stippledRectangularLines = true;
		break;
	case VK_LINE_RASTERIZATION_MODE_RECTANGULAR:
		instance->core14.rectangularLines = true;
		if (stippledLineEnable == VK_TRUE) instance->core14.stippledRectangularLines = true;
		break;
	case VK_LINE_RASTERIZATION_MODE_BRESENHAM:
		instance->core14.bresenhamLines = true;
		if (stippledLineEnable == VK_TRUE) instance->core14.stippledBresenhamLines = true;
		break;
	case VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH:
		instance->core14.smoothLines = true;
		if (stippledLineEnable == VK_TRUE) instance->core14.stippledSmoothLines = true;
		break;
	default:
		break;
	}
}

static bool render_pass_uses_multiview(const VkRenderPassCreateInfo* info)
{
	const VkRenderPassMultiviewCreateInfo* multiview = (const VkRenderPassMultiviewCreateInfo*)get_extension(info, VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO);
	if (!multiview) return false;

	assert(multiview->subpassCount == 0 || multiview->subpassCount == info->subpassCount);
	assert(multiview->dependencyCount == 0 || multiview->dependencyCount == info->dependencyCount);
	assert(info->dependencyCount == 0 || info->pDependencies != nullptr);

	if (array_has_nonzero(multiview->pViewMasks, multiview->subpassCount)) return true;
	if (array_has_nonzero(multiview->pCorrelationMasks, multiview->correlationMaskCount)) return true;
	if (array_has_nonzero(multiview->pViewOffsets, multiview->dependencyCount)) return true;

	for (uint32_t i = 0; i < multiview->dependencyCount; i++)
	{
		if (info->pDependencies[i].dependencyFlags & VK_DEPENDENCY_VIEW_LOCAL_BIT) return true;
	}

	return false;
}

static bool render_pass_uses_multiview(const VkRenderPassCreateInfo2* info)
{
	assert(info->subpassCount == 0 || info->pSubpasses != nullptr);
	assert(info->dependencyCount == 0 || info->pDependencies != nullptr);

	for (uint32_t i = 0; i < info->subpassCount; i++)
	{
		if (info->pSubpasses[i].viewMask != 0) return true;
	}

	for (uint32_t i = 0; i < info->dependencyCount; i++)
	{
		if (info->pDependencies[i].dependencyFlags & VK_DEPENDENCY_VIEW_LOCAL_BIT) return true;
		if (info->pDependencies[i].viewOffset != 0) return true;
	}

	return array_has_nonzero(info->pCorrelatedViewMasks, info->correlatedViewMaskCount);
}

static void parse_SPIRV(const uint32_t* code, uint32_t code_size)
{
	const uint32_t* insn = code + 5;
	const uint32_t* end = code + code_size / 4;
	while (insn < end)
	{
		const uint16_t opcode = uint16_t(insn[0]);
		const uint16_t word_count = uint16_t(insn[0] >> 16);
		assert(word_count > 0);
		if (opcode == SpvOpCapability)
		{
			switch (insn[1])
			{
			case SpvCapabilityGeometry: instance->core10.geometryShader = true; break;
			case SpvCapabilityTessellation: instance->core10.tessellationShader = true; break;
			case SpvCapabilityImageGatherExtended: instance->core10.shaderImageGatherExtended = true; break;
			case SpvCapabilityUniformBufferArrayDynamicIndexing: instance->core10.shaderUniformBufferArrayDynamicIndexing = true; break;
			case SpvCapabilitySampledImageArrayDynamicIndexing: instance->core10.shaderSampledImageArrayDynamicIndexing = true; break;
			case SpvCapabilityStorageBufferArrayDynamicIndexing: instance->core10.shaderStorageBufferArrayDynamicIndexing = true; break;
			case SpvCapabilityStorageImageArrayDynamicIndexing: instance->core10.shaderStorageImageArrayDynamicIndexing = true; break;
			case SpvCapabilityClipDistance: instance->core10.shaderClipDistance = true; break;
			case SpvCapabilityCullDistance: instance->core10.shaderCullDistance = true; break;
			case SpvCapabilityFloat64: instance->core10.shaderFloat64 = true; break;
			case SpvCapabilityInt64: instance->core10.shaderInt64 = true; break;
			case SpvCapabilityInt16: instance->core10.shaderInt16 = true; break;
			case SpvCapabilityMinLod: instance->core10.shaderResourceMinLod = true; break;
			case SpvCapabilitySampledCubeArray: instance->core10.imageCubeArray = true; break;
			case SpvCapabilityImageCubeArray: instance->core10.imageCubeArray = true; break;
			case SpvCapabilitySparseResidency: instance->core10.shaderResourceResidency = true; break;
			case SpvCapabilityStorageBuffer16BitAccess: instance->core11.storageBuffer16BitAccess = true; break;
			case SpvCapabilityUniformAndStorageBuffer16BitAccess: instance->core11.uniformAndStorageBuffer16BitAccess = true; break;
			case SpvCapabilityStoragePushConstant16: instance->core11.storagePushConstant16 = true; break;
			case SpvCapabilityStorageInputOutput16: instance->core11.storageInputOutput16 = true; break;
			case SpvCapabilityMultiView: instance->core11.multiview = true; instance->has_VK_KHR_multiview = true; break;
			case SpvCapabilityVariablePointersStorageBuffer: instance->core11.variablePointersStorageBuffer = true; break;
			case SpvCapabilityVariablePointers: instance->core11.variablePointers = true; break;
			case SpvCapabilityDrawParameters: instance->core11.shaderDrawParameters = true; break;
			case SpvCapabilityDotProductInputAllKHR: instance->core13.shaderIntegerDotProduct = true; break;
			case SpvCapabilityDotProductInput4x8BitKHR: instance->core13.shaderIntegerDotProduct = true; break;
			case SpvCapabilityDotProductInput4x8BitPackedKHR: instance->core13.shaderIntegerDotProduct = true; break;
			case SpvCapabilityDotProductKHR: instance->core13.shaderIntegerDotProduct = true; break;
			case SpvCapabilityGroupNonUniformRotateKHR: instance->core14.shaderSubgroupRotate = true; break;
			case SpvCapabilityExpectAssumeKHR: instance->core14.shaderExpectAssume = true; break;
			case SpvCapabilityFloatControls2: instance->core14.shaderFloatControls2 = true; break;
			case SpvCapabilityRayQueryKHR: instance->has_VK_KHR_ray_query = true; break;
			case SpvCapabilityRayCullMaskKHR: instance->has_VK_KHR_ray_tracing_maintenance1 = true; break;
			case SpvCapabilityRayTracingKHR: instance->has_VK_KHR_ray_tracing_pipeline = true; break;
			case SpvCapabilityRayTraversalPrimitiveCullingKHR:
				instance->has_VK_KHR_ray_tracing_pipeline = true;
				instance->has_VK_KHR_ray_query = true;
				break;
			case SpvCapabilityTensorsARM: instance->has_VK_ARM_tensors = true; break;
			case SpvCapabilityStorageTensorArrayDynamicIndexingARM: instance->has_VK_ARM_tensors = true; break;
			case SpvCapabilityStorageTensorArrayNonUniformIndexingARM: instance->has_VK_ARM_tensors = true; break;
			case SpvCapabilityStorageBuffer8BitAccess: instance->core12.storageBuffer8BitAccess = true; break;
			case SpvCapabilityUniformAndStorageBuffer8BitAccess: instance->core12.uniformAndStorageBuffer8BitAccess = true; break;
			case SpvCapabilityStoragePushConstant8: instance->core12.storagePushConstant8 = true; break;
			case SpvCapabilityFloat16: instance->core12.shaderFloat16 = true; break;
			case SpvCapabilityInt8: instance->core12.shaderInt8 = true; break;
			case SpvCapabilityInputAttachmentArrayDynamicIndexing: instance->core12.shaderInputAttachmentArrayDynamicIndexing = true; break;
			case SpvCapabilityUniformTexelBufferArrayDynamicIndexing: instance->core12.shaderUniformTexelBufferArrayDynamicIndexing = true; break;
			case SpvCapabilityStorageTexelBufferArrayDynamicIndexing: instance->core12.shaderStorageTexelBufferArrayDynamicIndexing = true; break;
			case SpvCapabilityUniformBufferArrayNonUniformIndexing: instance->core12.shaderUniformBufferArrayNonUniformIndexing = true; break;
			case SpvCapabilitySampledImageArrayNonUniformIndexing: instance->core12.shaderSampledImageArrayNonUniformIndexing = true; break;
			case SpvCapabilityStorageBufferArrayNonUniformIndexing: instance->core12.shaderStorageBufferArrayNonUniformIndexing = true; break;
			case SpvCapabilityStorageImageArrayNonUniformIndexing: instance->core12.shaderStorageImageArrayNonUniformIndexing = true; break;
			case SpvCapabilityInputAttachmentArrayNonUniformIndexing: instance->core12.shaderInputAttachmentArrayNonUniformIndexing = true; break;
			case SpvCapabilityUniformTexelBufferArrayNonUniformIndexing: instance->core12.shaderUniformTexelBufferArrayNonUniformIndexing = true; break;
			case SpvCapabilityStorageTexelBufferArrayNonUniformIndexing: instance->core12.shaderStorageTexelBufferArrayNonUniformIndexing = true; break;
			case SpvCapabilityRuntimeDescriptorArray: instance->core12.runtimeDescriptorArray = true; break;
			case SpvCapabilityVulkanMemoryModel: instance->core12.vulkanMemoryModel = true; break;
			case SpvCapabilityVulkanMemoryModelDeviceScope: instance->core12.vulkanMemoryModelDeviceScope = true; break;
			case SpvCapabilityShaderViewportIndex: instance->core12.shaderOutputViewportIndex = true; break;
			case SpvCapabilityShaderLayer: instance->core12.shaderOutputLayer = true; break;
			case SpvCapabilityShaderViewportIndexLayerEXT: instance->has_VK_EXT_shader_viewport_index_layer = true; break;
			case SpvCapabilityRayTracingOpacityMicromapEXT: instance->has_VK_EXT_opacity_micromap = true; break;
			case SpvCapabilityTransformFeedback: instance->has_VK_EXT_transform_feedback = true; break;
			case SpvCapabilityGeometryStreams: instance->has_VK_EXT_transform_feedback = true; break;
			default: break;
			}
		}
		else if (opcode == SpvOpDecorate && word_count >= 4 &&
		         insn[2] == SpvDecorationBuiltIn && insn[3] == SpvBuiltInPointSize)
		{
			instance->core10.largePoints = true;
		}
		else if (opcode == SpvOpDemoteToHelperInvocationEXT)
		{
			instance->core13.shaderDemoteToHelperInvocation = true;
		}
		insn += word_count;
	}
}

// --- Checking structures helper functions ---

void struct_check_VkPipelineShaderStageCreateInfo(const VkPipelineShaderStageCreateInfo* info)
{
	if (info->stage == VK_SHADER_STAGE_GEOMETRY_BIT) instance->core10.geometryShader = true;
	else if (info->stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || info->stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) instance->core10.tessellationShader = true;

	if (info->flags & VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT) instance->core13.subgroupSizeControl = true;
	if (get_extension(info, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO)) instance->core13.subgroupSizeControl = true;
}

void struct_check_VkPipelineColorBlendAttachmentState(const VkPipelineColorBlendAttachmentState* info)
{
	const VkBlendFactor factors[4] = { VK_BLEND_FACTOR_SRC1_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR, VK_BLEND_FACTOR_SRC1_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA };
	for (int i = 0; i < 4; i++) if (info->srcColorBlendFactor == factors[i]) instance->core10.dualSrcBlend = true;
	for (int i = 0; i < 4; i++) if (info->dstColorBlendFactor == factors[i]) instance->core10.dualSrcBlend = true;
	for (int i = 0; i < 4; i++) if (info->srcAlphaBlendFactor == factors[i]) instance->core10.dualSrcBlend = true;
	for (int i = 0; i < 4; i++) if (info->dstAlphaBlendFactor == factors[i]) instance->core10.dualSrcBlend = true;
}

void struct_check_VkPipelineColorBlendStateCreateInfo(const VkPipelineColorBlendStateCreateInfo* info)
{
	if (info->logicOpEnable == VK_TRUE) instance->core10.logicOp = true;

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
				instance->core10.independentBlend = true;
			}
			struct_check_VkPipelineColorBlendAttachmentState(&info->pAttachments[i]);
		}
	}
}

void struct_check_VkPipelineMultisampleStateCreateInfo(const VkPipelineMultisampleStateCreateInfo* info)
{
	if (info->alphaToOneEnable == VK_TRUE) instance->core10.alphaToOne = true;
	if (info->sampleShadingEnable == VK_TRUE) instance->core10.sampleRateShading = true;
}

void struct_check_VkPipelineRasterizationStateCreateInfo(const VkPipelineRasterizationStateCreateInfo* info)
{
	if (info->depthClampEnable == VK_TRUE) instance->core10.depthClamp = true;
	if (info->polygonMode == VK_POLYGON_MODE_POINT || info->polygonMode == VK_POLYGON_MODE_LINE) instance->core10.fillModeNonSolid = true;

	const VkPipelineRasterizationStateStreamCreateInfoEXT* stream = (const VkPipelineRasterizationStateStreamCreateInfoEXT*)get_extension(
		info, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT);
	if (stream) instance->has_VK_EXT_transform_feedback = true;

	const VkPipelineRasterizationLineStateCreateInfo* line = (const VkPipelineRasterizationLineStateCreateInfo*)get_extension(
		info, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO);
	if (line) mark_line_rasterization_mode_usage(line->lineRasterizationMode, line->stippledLineEnable);
}

void struct_check_VkPipelineDepthStencilStateCreateInfo(const VkPipelineDepthStencilStateCreateInfo* info)
{
	if (info->depthBoundsTestEnable == VK_TRUE) instance->core10.depthBounds = true;
}

void struct_check_VkPipelineViewportStateCreateInfo(const VkPipelineViewportStateCreateInfo* info)
{
	if (info->viewportCount > 1 || info->scissorCount > 1)
	{
		instance->core10.multiViewport = true;
	}
}

static bool enables_extension(const std::string& name, const char* const* ppEnabledExtensionNames, uint32_t enabledExtensionCount)
{
	for (uint32_t i = 0; i < enabledExtensionCount; i++) if (name == ppEnabledExtensionNames[i]) return true;
	return false;
}

static void check_prune_device(const std::vector<std::string>& aliases, VkDeviceCreateInfo* info, VkStructureType sType, const std::unordered_set<std::string>& enabled_exts, std::unordered_set<std::string>& found)
{
	bool none_found = true;
	for (const auto& v : aliases) { if (enabled_exts.count(v) != 0) none_found = false; }
	if (none_found && prune_extension(info, sType)) for (const auto& v : aliases) if (enables_extension(v, info->ppEnabledExtensionNames, info->enabledExtensionCount)) found.insert(v);
}

static bool preserve_acceleration_structure_dependency(const std::unordered_set<std::string>& exts)
{
	if (exts.count("VK_KHR_acceleration_structure") == 0) return false;
	if (exts.count("VK_KHR_ray_query") != 0 && instance->has_VK_KHR_ray_query) return true;
	if (exts.count("VK_KHR_ray_tracing_pipeline") != 0 && instance->has_VK_KHR_ray_tracing_pipeline) return true;
	if (exts.count("VK_KHR_ray_tracing_maintenance1") != 0 && instance->has_VK_KHR_ray_tracing_maintenance1) return true;
	if (exts.count("VK_EXT_opacity_micromap") != 0 && instance->has_VK_EXT_opacity_micromap) return true;
	if (exts.count("VK_EXT_opacity_micromap") != 0 && exts.count("VK_ARM_pipeline_opacity_micromap") != 0 &&
	    instance->has_VK_ARM_pipeline_opacity_micromap) return true;
	return false;
}

static bool preserve_synchronization2_dependency(const std::unordered_set<std::string>& exts)
{
	return exts.count("VK_KHR_synchronization2") != 0 && uses_pre13_synchronization2() &&
	       exts.count("VK_EXT_opacity_micromap") != 0 &&
	       (instance->has_VK_EXT_opacity_micromap ||
	        (exts.count("VK_ARM_pipeline_opacity_micromap") != 0 && instance->has_VK_ARM_pipeline_opacity_micromap));
}

std::unordered_set<std::string> feature_detection::adjust_VkDeviceCreateInfo(VkDeviceCreateInfo* info, const std::unordered_set<std::string>& enabled_exts) const
{
	std::unordered_set<std::string> found;
	check_prune_device({"VK_KHR_shader_atomic_int64"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, enabled_exts, found);
	check_prune_device({"VK_EXT_shader_image_atomic_int64"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT, enabled_exts, found);
	check_prune_device({"VK_KHR_multiview"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES, enabled_exts, found);
	check_prune_device({"VK_KHR_dynamic_rendering"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, enabled_exts, found);
	check_prune_device({"VK_KHR_synchronization2"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES, enabled_exts, found);
	check_prune_device({"VK_KHR_acceleration_structure"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, enabled_exts, found);
	check_prune_device({"VK_KHR_ray_query"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, enabled_exts, found);
	check_prune_device({"VK_KHR_ray_tracing_pipeline"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, enabled_exts, found);
	check_prune_device({"VK_KHR_ray_tracing_maintenance1"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, enabled_exts, found);
	check_prune_device({"VK_EXT_descriptor_heap"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT, enabled_exts, found);
	check_prune_device({"VK_EXT_opacity_micromap"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT, enabled_exts, found);
	check_prune_device({"VK_ARM_pipeline_opacity_micromap"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_OPACITY_MICROMAP_FEATURES_ARM, enabled_exts, found);
	check_prune_device({"VK_KHR_robustness2", "VK_EXT_robustness2"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, enabled_exts, found);
	check_prune_device({"VK_EXT_transform_feedback"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, enabled_exts, found);
	check_prune_device({"VK_ARM_tensors"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM, enabled_exts, found);
	check_prune_device({"VK_ARM_tensors"}, info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_FEATURES_ARM, enabled_exts, found);
	return found;
}

std::unordered_set<std::string> feature_detection::adjust_VkInstanceCreateInfo(VkInstanceCreateInfo* info, const std::unordered_set<std::string>& enabled_exts) const
{
	std::unordered_set<std::string> found;
	return found;
}

std::unordered_set<std::string> feature_detection::adjust_device_extensions(std::unordered_set<std::string>& exts) const
{
	std::unordered_set<std::string> removed;
	const bool preserve_acceleration_structure = preserve_acceleration_structure_dependency(exts);
	const bool preserve_synchronization2 = preserve_synchronization2_dependency(exts);
	if (!has_VkPhysicalDeviceShaderAtomicInt64Features) removed.insert(exts.extract("VK_KHR_shader_atomic_int64"));
	if (!has_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT) removed.insert(exts.extract("VK_EXT_shader_image_atomic_int64")); // alias of above
	if (!has_VK_KHR_shared_presentable_image) removed.insert(exts.extract("VK_KHR_shared_presentable_image"));
	if (!has_VK_EXT_external_memory_host) removed.insert(exts.extract("VK_EXT_external_memory_host"));
	if (!has_VK_IMG_filter_cubic) removed.insert(exts.extract("VK_IMG_filter_cubic"));
	if (!has_VK_KHR_bind_memory2) removed.insert(exts.extract("VK_KHR_bind_memory2"));
	if (!has_VK_KHR_create_renderpass2) removed.insert(exts.extract("VK_KHR_create_renderpass2"));
	if (!has_VK_KHR_copy_commands2) removed.insert(exts.extract("VK_KHR_copy_commands2"));
	if (!has_VK_KHR_dynamic_rendering) removed.insert(exts.extract("VK_KHR_dynamic_rendering"));
	if (!has_VK_KHR_get_memory_requirements2) removed.insert(exts.extract("VK_KHR_get_memory_requirements2"));
	if (!has_VK_KHR_maintenance1 && requested_instance_api_version >= VK_API_VERSION_1_1) removed.insert(exts.extract("VK_KHR_maintenance1"));
	if (!has_VK_KHR_external_memory) removed.insert(exts.extract("VK_KHR_external_memory"));
	if (!has_VK_KHR_external_memory_fd) removed.insert(exts.extract("VK_KHR_external_memory_fd"));
	if (!has_VK_KHR_map_memory2) removed.insert(exts.extract("VK_KHR_map_memory2"));
	if (!has_VK_KHR_multiview) removed.insert(exts.extract("VK_KHR_multiview"));
	if (!has_VK_KHR_synchronization2 && !preserve_synchronization2) removed.insert(exts.extract("VK_KHR_synchronization2"));
	if (!has_VK_KHR_acceleration_structure && !preserve_acceleration_structure) removed.insert(exts.extract("VK_KHR_acceleration_structure"));
	if (!has_VK_KHR_ray_query) removed.insert(exts.extract("VK_KHR_ray_query"));
	if (!has_VK_ARM_tensors) removed.insert(exts.extract("VK_ARM_tensors"));
	if (!has_VK_KHR_ray_tracing_pipeline) removed.insert(exts.extract("VK_KHR_ray_tracing_pipeline"));
	if (!has_VK_KHR_ray_tracing_maintenance1) removed.insert(exts.extract("VK_KHR_ray_tracing_maintenance1"));
	if (!has_VK_KHR_robustness2) removed.insert(exts.extract("VK_KHR_robustness2"));
	if (!has_VK_EXT_descriptor_heap) removed.insert(exts.extract("VK_EXT_descriptor_heap"));
	if (!has_VK_EXT_opacity_micromap && !(exts.count("VK_ARM_pipeline_opacity_micromap") != 0 && has_VK_ARM_pipeline_opacity_micromap))
		removed.insert(exts.extract("VK_EXT_opacity_micromap"));
	if (!has_VK_ARM_pipeline_opacity_micromap) removed.insert(exts.extract("VK_ARM_pipeline_opacity_micromap"));
	if (!has_VK_EXT_robustness2) removed.insert(exts.extract("VK_EXT_robustness2"));
	if (!has_VK_EXT_shader_viewport_index_layer) removed.insert(exts.extract("VK_EXT_shader_viewport_index_layer"));
	if (!has_VK_EXT_transform_feedback) removed.insert(exts.extract("VK_EXT_transform_feedback"));
	return removed;
}

std::unordered_set<std::string> feature_detection::adjust_instance_extensions(std::unordered_set<std::string>& exts) const
{
	std::unordered_set<std::string> removed;
	if (!has_VK_EXT_swapchain_colorspace) removed.insert(exts.extract("VK_EXT_swapchain_colorspace"));
	if (!has_VK_KHR_get_physical_device_properties2) removed.insert(exts.extract("VK_KHR_get_physical_device_properties2"));
	if (!has_VK_KHR_external_fence_capabilities) removed.insert(exts.extract("VK_KHR_external_fence_capabilities"));
	return removed;
}

std::unordered_set<std::string> feature_detection::adjust_VkPhysicalDeviceFeatures(VkPhysicalDeviceFeatures& incore10) const
{
	std::unordered_set<std::string> found;
	// Only turn off the features we have checking code for
	#define CHECK_FEATURE10(_x) if (!core10._x && incore10._x) { incore10._x = false; found.insert(# _x); }
	CHECK_FEATURE10(fullDrawIndexUint32);
	CHECK_FEATURE10(dualSrcBlend);
	CHECK_FEATURE10(geometryShader);
	CHECK_FEATURE10(tessellationShader);
	CHECK_FEATURE10(sampleRateShading);
	CHECK_FEATURE10(depthClamp);
	CHECK_FEATURE10(depthBiasClamp);
	CHECK_FEATURE10(wideLines);
	CHECK_FEATURE10(largePoints);
	CHECK_FEATURE10(samplerAnisotropy);
	CHECK_FEATURE10(textureCompressionETC2);
	CHECK_FEATURE10(textureCompressionASTC_LDR);
	CHECK_FEATURE10(textureCompressionBC);
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
	return found;
}

std::unordered_set<std::string> feature_detection::adjust_VkPhysicalDeviceVulkan11Features(VkPhysicalDeviceVulkan11Features& incore11) const
{
	std::unordered_set<std::string> found;
	// Only turn off the features we have checking code for
	#define CHECK_FEATURE11(_x) if (!core11._x && incore11._x) { incore11._x = false; found.insert(# _x); }
	CHECK_FEATURE11(storageBuffer16BitAccess);
	CHECK_FEATURE11(uniformAndStorageBuffer16BitAccess);
	CHECK_FEATURE11(storagePushConstant16);
	CHECK_FEATURE11(storageInputOutput16);
	CHECK_FEATURE11(multiview);
	if (!(core11.multiview && core10.geometryShader) && incore11.multiviewGeometryShader)
	{
		incore11.multiviewGeometryShader = false;
		found.insert("multiviewGeometryShader");
	}
	if (!(core11.multiview && core10.tessellationShader) && incore11.multiviewTessellationShader)
	{
		incore11.multiviewTessellationShader = false;
		found.insert("multiviewTessellationShader");
	}
	CHECK_FEATURE11(variablePointersStorageBuffer);
	CHECK_FEATURE11(variablePointers);
	CHECK_FEATURE11(shaderDrawParameters);
	#undef CHECK_FEATURE11
	return found;
}

std::unordered_set<std::string> feature_detection::adjust_VkPhysicalDeviceVulkan12Features(VkPhysicalDeviceVulkan12Features& incore12) const
{
	std::unordered_set<std::string> found;
	// Only turn off the features we have checking code for
	#define CHECK_FEATURE12(_x) if (!core12._x && incore12._x) { incore12._x = false; found.insert(# _x); }
	CHECK_FEATURE12(drawIndirectCount);
	CHECK_FEATURE12(hostQueryReset);
	CHECK_FEATURE12(samplerMirrorClampToEdge);
	CHECK_FEATURE12(bufferDeviceAddress);
	CHECK_FEATURE12(bufferDeviceAddressCaptureReplay);
	if (!core12.bufferDeviceAddress && incore12.bufferDeviceAddressMultiDevice)
	{
		incore12.bufferDeviceAddressMultiDevice = false;
		found.insert("bufferDeviceAddressMultiDevice");
	}
	CHECK_FEATURE12(timelineSemaphore);
	CHECK_FEATURE12(storageBuffer8BitAccess);
	CHECK_FEATURE12(uniformAndStorageBuffer8BitAccess);
	CHECK_FEATURE12(storagePushConstant8);
	CHECK_FEATURE12(shaderFloat16);
	CHECK_FEATURE12(shaderInt8);
	CHECK_FEATURE12(shaderInputAttachmentArrayDynamicIndexing);
	CHECK_FEATURE12(shaderUniformTexelBufferArrayDynamicIndexing);
	CHECK_FEATURE12(shaderStorageTexelBufferArrayDynamicIndexing);
	CHECK_FEATURE12(shaderUniformBufferArrayNonUniformIndexing);
	CHECK_FEATURE12(shaderSampledImageArrayNonUniformIndexing);
	CHECK_FEATURE12(shaderStorageBufferArrayNonUniformIndexing);
	CHECK_FEATURE12(shaderStorageImageArrayNonUniformIndexing);
	CHECK_FEATURE12(shaderInputAttachmentArrayNonUniformIndexing);
	CHECK_FEATURE12(shaderUniformTexelBufferArrayNonUniformIndexing);
	CHECK_FEATURE12(shaderStorageTexelBufferArrayNonUniformIndexing);
	CHECK_FEATURE12(runtimeDescriptorArray);
	CHECK_FEATURE12(vulkanMemoryModel);
	CHECK_FEATURE12(vulkanMemoryModelDeviceScope);
	CHECK_FEATURE12(shaderOutputViewportIndex);
	CHECK_FEATURE12(shaderOutputLayer);
	#undef CHECK_FEATURE12
	return found;
}

std::unordered_set<std::string> feature_detection::adjust_VkPhysicalDeviceVulkan13Features(VkPhysicalDeviceVulkan13Features& incore13) const
{
	std::unordered_set<std::string> found;
	// Only turn off the features we have checking code for
	#define CHECK_FEATURE13(_x) if (!core13._x && incore13._x) { incore13._x = false; found.insert(# _x); }
	CHECK_FEATURE13(dynamicRendering);
	CHECK_FEATURE13(shaderDemoteToHelperInvocation);
	CHECK_FEATURE13(shaderIntegerDotProduct);
	CHECK_FEATURE13(subgroupSizeControl);
	CHECK_FEATURE13(synchronization2);
	#undef CHECK_FEATURE13
	return found;
}

std::unordered_set<std::string> feature_detection::adjust_VkPhysicalDeviceVulkan14Features(VkPhysicalDeviceVulkan14Features& incore14) const
{
	std::unordered_set<std::string> found;
	// Only turn off the features we have checking code for
	#define CHECK_FEATURE14(_x) if (!core14._x && incore14._x) { incore14._x = false; found.insert(# _x); }
	CHECK_FEATURE14(shaderSubgroupRotate);
	CHECK_FEATURE14(shaderExpectAssume);
	CHECK_FEATURE14(shaderFloatControls2);
	CHECK_FEATURE14(rectangularLines);
	CHECK_FEATURE14(bresenhamLines);
	CHECK_FEATURE14(smoothLines);
	CHECK_FEATURE14(stippledRectangularLines);
	CHECK_FEATURE14(stippledBresenhamLines);
	CHECK_FEATURE14(stippledSmoothLines);
	CHECK_FEATURE14(indexTypeUint8);
	#undef CHECK_FEATURE14
	return found;
}

// --- Checking functions. Call these for all these Vulkan commands after they are successfully called, before returning. ---

VkResult check_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
	assert(pCreateInfo && pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
	if (pCreateInfo && pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->apiVersion != 0) instance->requested_instance_api_version = pCreateInfo->pApplicationInfo->apiVersion;
	return VK_SUCCESS;
}

VkResult check_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule)
{
	parse_SPIRV(pCreateInfo->pCode, pCreateInfo->codeSize);
	return VK_SUCCESS;
}

VkResult check_vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore)
{
	VkSemaphoreTypeCreateInfo* stci = (VkSemaphoreTypeCreateInfo*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
	if (stci && stci->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE) instance->core12.timelineSemaphore = true;
	return VK_SUCCESS;
}

VkResult check_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
	for (uint32_t i = 0; i < createInfoCount; i++)
	{
		const VkPipelineRenderingCreateInfo* rendering_info =
			(const VkPipelineRenderingCreateInfo*)get_extension(pCreateInfos[i].pNext, VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
		if (rendering_info && uses_pre13_dynamic_rendering()) instance->has_VK_KHR_dynamic_rendering = true;
		if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT) instance->has_VK_EXT_opacity_micromap = true;
		if (pipeline_flags2_uses_pipeline_opacity_micromap(pCreateInfos[i].pNext)) instance->has_VK_ARM_pipeline_opacity_micromap = true;
		if (pCreateInfos[i].pRasterizationState && pCreateInfos[i].pRasterizationState->depthBiasClamp != 0.0) instance->core10.depthBiasClamp = true;
		if (pCreateInfos[i].pRasterizationState && pCreateInfos[i].pRasterizationState->lineWidth != 1.0) instance->core10.wideLines = true;
		for (uint32_t stage_index = 0; stage_index < pCreateInfos[i].stageCount; stage_index++)
		{
			struct_check_VkPipelineShaderStageCreateInfo(&pCreateInfos[i].pStages[stage_index]);
		}
		if (pCreateInfos[i].pColorBlendState) struct_check_VkPipelineColorBlendStateCreateInfo(pCreateInfos[i].pColorBlendState);
		if (pCreateInfos[i].pMultisampleState) struct_check_VkPipelineMultisampleStateCreateInfo(pCreateInfos[i].pMultisampleState);
		if (pCreateInfos[i].pRasterizationState) struct_check_VkPipelineRasterizationStateCreateInfo(pCreateInfos[i].pRasterizationState);
		if (pCreateInfos[i].pDepthStencilState) struct_check_VkPipelineDepthStencilStateCreateInfo(pCreateInfos[i].pDepthStencilState);
		if (pCreateInfos[i].pViewportState) struct_check_VkPipelineViewportStateCreateInfo(pCreateInfos[i].pViewportState);
	}
	return VK_SUCCESS;
}

VkResult check_vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
	for (uint32_t i = 0; i < createInfoCount; i++)
	{
		if (pipeline_flags2_uses_pipeline_opacity_micromap(pCreateInfos[i].pNext)) instance->has_VK_ARM_pipeline_opacity_micromap = true;
		struct_check_VkPipelineShaderStageCreateInfo(&pCreateInfos[i].stage);
	}
	return VK_SUCCESS;
}

VkResult check_vkCreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
	instance->has_VK_KHR_ray_tracing_pipeline = true;
	for (uint32_t i = 0; i < createInfoCount; i++)
	{
		if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT) instance->has_VK_EXT_opacity_micromap = true;
		if (pipeline_flags2_uses_pipeline_opacity_micromap(pCreateInfos[i].pNext)) instance->has_VK_ARM_pipeline_opacity_micromap = true;
		for (uint32_t stage_index = 0; stage_index < pCreateInfos[i].stageCount; stage_index++)
		{
			struct_check_VkPipelineShaderStageCreateInfo(&pCreateInfos[i].pStages[stage_index]);
		}
	}
	return VK_SUCCESS;
}

VkResult check_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
	// If we need to check the feature enable struct to know whether an extension is used, we check it here.

	const VkPhysicalDeviceShaderAtomicInt64Features* pdsai64f = (VkPhysicalDeviceShaderAtomicInt64Features*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES);
	if (pdsai64f && (pdsai64f->shaderBufferInt64Atomics || pdsai64f->shaderSharedInt64Atomics)) instance->has_VkPhysicalDeviceShaderAtomicInt64Features = true;

	const VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT* pdsiai64f = (VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT);
	if (pdsiai64f && (pdsiai64f->shaderImageInt64Atomics || pdsiai64f->sparseImageInt64Atomics)) instance->has_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT = true;

	const VkPhysicalDeviceMultiviewFeatures* pdmf = (VkPhysicalDeviceMultiviewFeatures*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES);
	if (pdmf && (pdmf->multiview || pdmf->multiviewGeometryShader || pdmf->multiviewTessellationShader)) instance->has_VK_KHR_multiview = true;

	const VkPhysicalDeviceDynamicRenderingFeatures* pddrf =
		(const VkPhysicalDeviceDynamicRenderingFeatures*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES);
	if (pddrf && pddrf->dynamicRendering && uses_pre13_dynamic_rendering()) instance->has_VK_KHR_dynamic_rendering = true;

	const VkPhysicalDeviceSynchronization2Features* pds2f =
		(const VkPhysicalDeviceSynchronization2Features*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES);
	if (pds2f && pds2f->synchronization2 && uses_pre13_synchronization2()) instance->has_VK_KHR_synchronization2 = true;

	const VkPhysicalDeviceAccelerationStructureFeaturesKHR* pdasf =
		(const VkPhysicalDeviceAccelerationStructureFeaturesKHR*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
	if (pdasf && (pdasf->accelerationStructure || pdasf->accelerationStructureCaptureReplay || pdasf->accelerationStructureIndirectBuild ||
	              pdasf->accelerationStructureHostCommands || pdasf->descriptorBindingAccelerationStructureUpdateAfterBind))
	{
		instance->has_VK_KHR_acceleration_structure = true;
	}

	const VkPhysicalDeviceRayQueryFeaturesKHR* pdrqf =
		(const VkPhysicalDeviceRayQueryFeaturesKHR*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
	if (pdrqf && pdrqf->rayQuery) instance->has_VK_KHR_ray_query = true;

	const VkPhysicalDeviceRayTracingPipelineFeaturesKHR* pdrtpf =
		(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
	if (pdrtpf && (pdrtpf->rayTracingPipeline || pdrtpf->rayTracingPipelineShaderGroupHandleCaptureReplay ||
	               pdrtpf->rayTracingPipelineShaderGroupHandleCaptureReplayMixed || pdrtpf->rayTracingPipelineTraceRaysIndirect ||
	               pdrtpf->rayTraversalPrimitiveCulling))
	{
		instance->has_VK_KHR_ray_tracing_pipeline = true;
	}

	const VkPhysicalDeviceTensorFeaturesARM* pdtf =
		(const VkPhysicalDeviceTensorFeaturesARM*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM);
	if (has_enabled_tensor_features(pdtf)) instance->has_VK_ARM_tensors = true;

	const VkPhysicalDeviceDescriptorBufferTensorFeaturesARM* pddbtf =
		(const VkPhysicalDeviceDescriptorBufferTensorFeaturesARM*)get_extension(
			pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_FEATURES_ARM);
	if (pddbtf && pddbtf->descriptorBufferTensorDescriptors) instance->has_VK_ARM_tensors = true;

	const VkPhysicalDeviceDescriptorHeapFeaturesEXT* pddhf = (VkPhysicalDeviceDescriptorHeapFeaturesEXT*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT);
	if (pddhf && pddhf->descriptorHeap) instance->has_VK_EXT_descriptor_heap = true;

	// Older SDKs expose the robustness2 feature struct only through the EXT alias
	const VkPhysicalDeviceRobustness2FeaturesEXT* pdr2f = (VkPhysicalDeviceRobustness2FeaturesEXT*)get_extension(pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT);
	if (pdr2f && (pdr2f->robustBufferAccess2 || pdr2f->robustImageAccess2 || pdr2f->nullDescriptor))
	{
		instance->has_VK_KHR_robustness2 = true;
		instance->has_VK_EXT_robustness2 = true;
	}

	const VkPhysicalDeviceTransformFeedbackFeaturesEXT* pdtff = (const VkPhysicalDeviceTransformFeedbackFeaturesEXT*)get_extension(
		pCreateInfo, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT);
	if (pdtff && (pdtff->transformFeedback || pdtff->geometryStreams)) instance->has_VK_EXT_transform_feedback = true;

	return VK_SUCCESS;
}

VkResult check_vkCreateMicromapEXT(VkDevice device, const VkMicromapCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkMicromapEXT* pMicromap)
{
	instance->has_VK_EXT_opacity_micromap = true;
	return VK_SUCCESS;
}

void check_vkDestroyMicromapEXT(VkDevice device, VkMicromapEXT micromap, const VkAllocationCallbacks* pAllocator)
{
	instance->has_VK_EXT_opacity_micromap = true;
}

VkResult check_vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
	if (render_pass_uses_multiview(pCreateInfo))
	{
		instance->core11.multiview = true;
		instance->has_VK_KHR_multiview = true;
	}
	return VK_SUCCESS;
}

VkResult check_vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
	if (render_pass_uses_multiview(pCreateInfo))
	{
		instance->core11.multiview = true;
		instance->has_VK_KHR_multiview = true;
	}
	return VK_SUCCESS;
}

VkResult check_vkCreateRenderPass2KHR(VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
	instance->has_VK_KHR_create_renderpass2 = true;
	return check_vkCreateRenderPass2(device, pCreateInfo, pAllocator, pRenderPass);
}

void check_vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo)
{
}

void check_vkCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo)
{
	instance->has_VK_KHR_create_renderpass2 = true;
}

void check_vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo)
{
}

void check_vkCmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo)
{
	instance->has_VK_KHR_create_renderpass2 = true;
}

void check_vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo)
{
}

void check_vkCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo)
{
	instance->has_VK_KHR_create_renderpass2 = true;
}

void check_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures)
{
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT)) instance->has_VK_EXT_opacity_micromap = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_OPACITY_MICROMAP_FEATURES_ARM)) instance->has_VK_ARM_pipeline_opacity_micromap = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT)) instance->has_VK_EXT_transform_feedback = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM)) instance->has_VK_ARM_tensors = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_FEATURES_ARM)) instance->has_VK_ARM_tensors = true;
}

void check_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures)
{
	instance->has_VK_KHR_get_physical_device_properties2 = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT)) instance->has_VK_EXT_opacity_micromap = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_OPACITY_MICROMAP_FEATURES_ARM)) instance->has_VK_ARM_pipeline_opacity_micromap = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT)) instance->has_VK_EXT_transform_feedback = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM)) instance->has_VK_ARM_tensors = true;
	if (get_extension(pFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_FEATURES_ARM)) instance->has_VK_ARM_tensors = true;
}

void check_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties)
{
	if (get_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT)) instance->has_VK_EXT_opacity_micromap = true;
	if (get_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT)) instance->has_VK_EXT_transform_feedback = true;
	if (get_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_PROPERTIES_ARM)) instance->has_VK_ARM_tensors = true;
	if (get_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_PROPERTIES_ARM)) instance->has_VK_ARM_tensors = true;
}

void check_vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties)
{
	instance->has_VK_KHR_get_physical_device_properties2 = true;
	if (get_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT)) instance->has_VK_EXT_opacity_micromap = true;
	if (get_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT)) instance->has_VK_EXT_transform_feedback = true;
	if (get_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_PROPERTIES_ARM)) instance->has_VK_ARM_tensors = true;
	if (get_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_PROPERTIES_ARM)) instance->has_VK_ARM_tensors = true;
}

void check_vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties)
{
	if (get_extension(pFormatProperties, VK_STRUCTURE_TYPE_TENSOR_FORMAT_PROPERTIES_ARM)) instance->has_VK_ARM_tensors = true;
}

void check_vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties)
{
	instance->has_VK_KHR_get_physical_device_properties2 = true;
	if (get_extension(pFormatProperties, VK_STRUCTURE_TYPE_TENSOR_FORMAT_PROPERTIES_ARM)) instance->has_VK_ARM_tensors = true;
}

VkResult check_vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
                                                            VkImageFormatProperties2* pImageFormatProperties)
{
	instance->has_VK_KHR_get_physical_device_properties2 = true;
	return VK_SUCCESS;
}

void check_vkGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount,
                                                        VkQueueFamilyProperties2* pQueueFamilyProperties)
{
	instance->has_VK_KHR_get_physical_device_properties2 = true;
}

void check_vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
{
	instance->has_VK_KHR_get_physical_device_properties2 = true;
}

void check_vkGetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
                                                              uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties)
{
	instance->has_VK_KHR_get_physical_device_properties2 = true;
}

void check_vkGetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo,
                                                         VkExternalFenceProperties* pExternalFenceProperties)
{
	instance->has_VK_KHR_external_fence_capabilities = true;
}

VkResult check_vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities)
{
	VkSharedPresentSurfaceCapabilitiesKHR* sc = (VkSharedPresentSurfaceCapabilitiesKHR*)get_extension(pSurfaceCapabilities, VK_STRUCTURE_TYPE_SHARED_PRESENT_SURFACE_CAPABILITIES_KHR);
	if (sc && sc->sharedPresentSupportedUsageFlags) instance->has_VK_KHR_shared_presentable_image = true;
	return VK_SUCCESS;
}

VkResult check_vkCreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains)
{
	for (uint32_t i = 0; i < swapchainCount; i++)
	{
		if (is_colorspace_ext(pCreateInfos[i].imageColorSpace)) instance->has_VK_EXT_swapchain_colorspace = true;
	}
	return VK_SUCCESS;
}

VkResult check_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
	if (is_colorspace_ext(pCreateInfo->imageColorSpace)) instance->has_VK_EXT_swapchain_colorspace = true;
	return VK_SUCCESS;
}

VkResult check_vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler)
{
	if (pCreateInfo->anisotropyEnable == VK_TRUE) instance->core10.samplerAnisotropy = true;
	if (pCreateInfo->addressModeU == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
	    || pCreateInfo->addressModeV == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
	    || pCreateInfo->addressModeW == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE)
		instance->core12.samplerMirrorClampToEdge = true;
	if (pCreateInfo->magFilter == VK_FILTER_CUBIC_EXT || pCreateInfo->minFilter == VK_FILTER_CUBIC_EXT) instance->has_VK_IMG_filter_cubic = true;
	return VK_SUCCESS;
}

void check_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
{
	if (filter == VK_FILTER_CUBIC_EXT) instance->has_VK_IMG_filter_cubic = true;
}

VkResult vkCreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion)
{
	if (pCreateInfo->chromaFilter == VK_FILTER_CUBIC_EXT) instance->has_VK_IMG_filter_cubic = true;
	return VK_SUCCESS;
}

VkResult vkCreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion)
{
	if (pCreateInfo->chromaFilter == VK_FILTER_CUBIC_EXT) instance->has_VK_IMG_filter_cubic = true;
	return VK_SUCCESS;
}

VkResult check_vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
{
	if (pCreateInfo->queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS && pCreateInfo->pipelineStatistics != 0) instance->core10.pipelineStatisticsQuery = true;
	if (is_ray_tracing_maintenance1_query_type(pCreateInfo->queryType)) instance->has_VK_KHR_ray_tracing_maintenance1 = true;
	if (is_opacity_micromap_query_type(pCreateInfo->queryType)) instance->has_VK_EXT_opacity_micromap = true;
	if (pCreateInfo->queryType == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT) instance->has_VK_EXT_transform_feedback = true;
	return VK_SUCCESS;
}

VkResult check_vkCreateIndirectCommandsLayoutEXT(VkDevice device, const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                                 VkIndirectCommandsLayoutEXT* pIndirectCommandsLayout)
{
	if (indirect_commands_layout_uses_ray_tracing_maintenance1(pCreateInfo)) instance->has_VK_KHR_ray_tracing_maintenance1 = true;
	return VK_SUCCESS;
}

VkResult check_vkCreateImage(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
{
	const VkExternalMemoryImageCreateInfo* external_info = (const VkExternalMemoryImageCreateInfo*)get_extension(info, VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
	if (external_info) mark_external_memory_usage(external_info->handleTypes);

	if (is_etc2_format(info->format)) instance->core10.textureCompressionETC2 = true;
	if (is_astc_ldr_format(info->format)) instance->core10.textureCompressionASTC_LDR = true;
	if (is_bc_format(info->format)) instance->core10.textureCompressionBC = true;

	if (info->imageType == VK_IMAGE_TYPE_2D && info->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)
	{
		if (info->samples == VK_SAMPLE_COUNT_1_BIT) instance->core10.sparseResidencyImage2D = true;
		else if (info->samples == VK_SAMPLE_COUNT_2_BIT) instance->core10.sparseResidency2Samples = true;
		else if (info->samples == VK_SAMPLE_COUNT_4_BIT) instance->core10.sparseResidency4Samples = true;
		else if (info->samples == VK_SAMPLE_COUNT_8_BIT) instance->core10.sparseResidency8Samples = true;
		else if (info->samples == VK_SAMPLE_COUNT_16_BIT) instance->core10.sparseResidency16Samples = true;
	}
	else if (info->imageType == VK_IMAGE_TYPE_3D && info->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) instance->core10.sparseResidencyImage3D = true;

	if (info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) instance->core10.sparseBinding = true;
	if (info->flags & VK_IMAGE_CREATE_SPARSE_ALIASED_BIT) instance->core10.sparseResidencyAliased = true;
	if (info->imageType == VK_IMAGE_TYPE_3D && (info->flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT)) instance->has_VK_KHR_maintenance1 = true;
	if ((info->usage & VK_IMAGE_USAGE_STORAGE_BIT) && info->samples != VK_SAMPLE_COUNT_1_BIT) instance->core10.shaderStorageImageMultisample = true;
	return VK_SUCCESS;
}

VkResult check_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
	const VkExternalMemoryBufferCreateInfo* external_info = (const VkExternalMemoryBufferCreateInfo*)get_extension(info, VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO);
	if (external_info) mark_external_memory_usage(external_info->handleTypes);

	if (info->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
	{
		instance->core10.sparseBinding = true;
	}
	if (info->flags & VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT)
	{
		instance->core10.sparseResidencyBuffer = true;
	}
	if (info->flags & VK_BUFFER_CREATE_SPARSE_ALIASED_BIT) instance->core10.sparseResidencyAliased = true;
	if (info->usage & (VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR))
	{
		instance->core12.bufferDeviceAddress = true;
	}
	if (info->usage & (VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR))
	{
		instance->has_VK_KHR_acceleration_structure = true;
	}
	if (info->usage & (VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT))
	{
		instance->has_VK_EXT_transform_feedback = true;
	}
	if (info->usage & (VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT))
	{
		instance->has_VK_EXT_opacity_micromap = true;
	}
	return VK_SUCCESS;
}

VkResult check_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
	if (get_extension(pAllocateInfo, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_TENSOR_ARM)) instance->has_VK_ARM_tensors = true;

	const VkImportMemoryHostPointerInfoEXT* import_host_info = (const VkImportMemoryHostPointerInfoEXT*)get_extension(pAllocateInfo, VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT);
	if (import_host_info) instance->has_VK_EXT_external_memory_host = true;

	const VkImportMemoryFdInfoKHR* import_fd_info = (const VkImportMemoryFdInfoKHR*)get_extension(pAllocateInfo, VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
	if (import_fd_info) mark_external_memory_usage(import_fd_info->handleType);

	const VkExportMemoryAllocateInfo* export_info = (const VkExportMemoryAllocateInfo*)get_extension(pAllocateInfo, VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
	if (export_info) mark_external_memory_usage(export_info->handleTypes);

	return VK_SUCCESS;
}

VkResult check_vkGetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd)
{
	instance->has_VK_KHR_external_memory_fd = true;
	if (pGetFdInfo) mark_external_memory_usage(pGetFdInfo->handleType);
	return VK_SUCCESS;
}

VkResult check_vkGetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties)
{
	instance->has_VK_KHR_external_memory_fd = true;
	mark_external_memory_usage(handleType);
	return VK_SUCCESS;
}

VkResult check_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
{
	if (is_etc2_format(info->format)) instance->core10.textureCompressionETC2 = true;
	if (is_astc_ldr_format(info->format)) instance->core10.textureCompressionASTC_LDR = true;
	if (is_bc_format(info->format)) instance->core10.textureCompressionBC = true;
	if (info->viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) instance->core10.imageCubeArray = true;
	return VK_SUCCESS;
}

VkResult check_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
	assert(submitCount == 0 || pSubmits != nullptr);
	for (uint32_t i = 0; i < submitCount; i++) if (submit_pnext_uses_tensors(pSubmits[i].pNext)) instance->has_VK_ARM_tensors = true;
	return VK_SUCCESS;
}

VkResult check_vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence)
{
	assert(bindInfoCount == 0 || pBindInfo != nullptr);
	for (uint32_t i = 0; i < bindInfoCount; i++) if (submit_pnext_uses_tensors(pBindInfo[i].pNext)) instance->has_VK_ARM_tensors = true;
	return VK_SUCCESS;
}

VkResult check_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
	if (pPresentInfo && submit_pnext_uses_tensors(pPresentInfo->pNext)) instance->has_VK_ARM_tensors = true;
	return VK_SUCCESS;
}

void check_vkGetMicromapBuildSizesEXT(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkMicromapBuildInfoEXT* pBuildInfo,
                                      VkMicromapBuildSizesInfoEXT* pSizeInfo)
{
	instance->has_VK_EXT_opacity_micromap = true;
}

VkResult check_vkBuildMicromapsEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount, const VkMicromapBuildInfoEXT* pInfos)
{
	instance->has_VK_EXT_opacity_micromap = true;
	return VK_SUCCESS;
}

void check_vkCmdBuildMicromapsEXT(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkMicromapBuildInfoEXT* pInfos)
{
	instance->has_VK_EXT_opacity_micromap = true;
}

VkResult check_vkCopyMicromapEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMicromapInfoEXT* pInfo)
{
	instance->has_VK_EXT_opacity_micromap = true;
	return VK_SUCCESS;
}

VkResult check_vkCopyMicromapToMemoryEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMicromapToMemoryInfoEXT* pInfo)
{
	instance->has_VK_EXT_opacity_micromap = true;
	return VK_SUCCESS;
}

VkResult check_vkCopyMemoryToMicromapEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMemoryToMicromapInfoEXT* pInfo)
{
	instance->has_VK_EXT_opacity_micromap = true;
	return VK_SUCCESS;
}

VkResult check_vkWriteMicromapsPropertiesEXT(VkDevice device, uint32_t micromapCount, const VkMicromapEXT* pMicromaps, VkQueryType queryType, size_t dataSize,
                                             void* pData, size_t stride)
{
	if (is_opacity_micromap_query_type(queryType)) instance->has_VK_EXT_opacity_micromap = true;
	return VK_SUCCESS;
}

void check_vkCmdCopyMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapInfoEXT* pInfo)
{
	instance->has_VK_EXT_opacity_micromap = true;
}

void check_vkCmdCopyMicromapToMemoryEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapToMemoryInfoEXT* pInfo)
{
	instance->has_VK_EXT_opacity_micromap = true;
}

void check_vkCmdCopyMemoryToMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMemoryToMicromapInfoEXT* pInfo)
{
	instance->has_VK_EXT_opacity_micromap = true;
}

void check_vkCmdWriteMicromapsPropertiesEXT(VkCommandBuffer commandBuffer, uint32_t micromapCount, const VkMicromapEXT* pMicromaps, VkQueryType queryType,
                                            VkQueryPool queryPool, uint32_t firstQuery)
{
	if (is_opacity_micromap_query_type(queryType)) instance->has_VK_EXT_opacity_micromap = true;
}

void check_vkGetDeviceMicromapCompatibilityEXT(VkDevice device, const VkMicromapVersionInfoEXT* pVersionInfo,
                                               VkAccelerationStructureCompatibilityKHR* pCompatibility)
{
	instance->has_VK_EXT_opacity_micromap = true;
}

void check_vkGetAccelerationStructureBuildSizesKHR(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType,
                                                   const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo, const uint32_t* pMaxPrimitiveCounts,
                                                   VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo)
{
	if (build_info_uses_opacity_micromap(pBuildInfo)) instance->has_VK_EXT_opacity_micromap = true;
}

VkResult check_vkBuildAccelerationStructuresKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount,
                                                const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
                                                const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
	assert(infoCount == 0 || pInfos != nullptr);
	for (uint32_t i = 0; i < infoCount; i++)
	{
		if (build_info_uses_opacity_micromap(&pInfos[i])) instance->has_VK_EXT_opacity_micromap = true;
	}
	return VK_SUCCESS;
}

void check_vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                               const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
                                               const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
	assert(infoCount == 0 || pInfos != nullptr);
	for (uint32_t i = 0; i < infoCount; i++)
	{
		if (build_info_uses_opacity_micromap(&pInfos[i])) instance->has_VK_EXT_opacity_micromap = true;
	}
}

void check_vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress)
{
	instance->has_VK_KHR_ray_tracing_pipeline = true;
}

void check_vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize)
{
	instance->has_VK_KHR_ray_tracing_pipeline = true;
}

void check_vkCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress)
{
	instance->has_VK_KHR_ray_tracing_maintenance1 = true;
}

void check_vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo)
{
	instance->core13.synchronization2 = true;
	if (uses_pre13_synchronization2()) instance->has_VK_KHR_synchronization2 = true;
	if (dependency_info_uses_ray_tracing_maintenance1(pDependencyInfo)) instance->has_VK_KHR_ray_tracing_maintenance1 = true;
	if (dependency_info_uses_opacity_micromap(pDependencyInfo)) instance->has_VK_EXT_opacity_micromap = true;
	if (dependency_info_uses_tensors(pDependencyInfo)) instance->has_VK_ARM_tensors = true;
}

void check_vkCmdSetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo)
{
	instance->has_VK_KHR_synchronization2 = true;
	check_vkCmdSetEvent2(commandBuffer, event, pDependencyInfo);
}

void check_vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask)
{
	instance->core13.synchronization2 = true;
	if (uses_pre13_synchronization2()) instance->has_VK_KHR_synchronization2 = true;
	if (uses_ray_tracing_maintenance1_stage(stageMask)) instance->has_VK_KHR_ray_tracing_maintenance1 = true;
	if (uses_opacity_micromap_stage(stageMask)) instance->has_VK_EXT_opacity_micromap = true;
}

void check_vkCmdResetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask)
{
	instance->has_VK_KHR_synchronization2 = true;
	check_vkCmdResetEvent2(commandBuffer, event, stageMask);
}

void check_vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos)
{
	instance->core13.synchronization2 = true;
	if (uses_pre13_synchronization2()) instance->has_VK_KHR_synchronization2 = true;
	assert(eventCount == 0 || pEvents != nullptr);
	assert(eventCount == 0 || pDependencyInfos != nullptr);
	for (uint32_t i = 0; i < eventCount; i++)
	{
		if (dependency_info_uses_ray_tracing_maintenance1(&pDependencyInfos[i])) instance->has_VK_KHR_ray_tracing_maintenance1 = true;
		if (dependency_info_uses_opacity_micromap(&pDependencyInfos[i])) instance->has_VK_EXT_opacity_micromap = true;
		if (dependency_info_uses_tensors(&pDependencyInfos[i])) instance->has_VK_ARM_tensors = true;
	}
}

void check_vkCmdWaitEvents2KHR(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos)
{
	instance->has_VK_KHR_synchronization2 = true;
	check_vkCmdWaitEvents2(commandBuffer, eventCount, pEvents, pDependencyInfos);
}

void check_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo)
{
	instance->core13.synchronization2 = true;
	if (uses_pre13_synchronization2()) instance->has_VK_KHR_synchronization2 = true;
	if (dependency_info_uses_ray_tracing_maintenance1(pDependencyInfo)) instance->has_VK_KHR_ray_tracing_maintenance1 = true;
	if (dependency_info_uses_opacity_micromap(pDependencyInfo)) instance->has_VK_EXT_opacity_micromap = true;
	if (dependency_info_uses_tensors(pDependencyInfo)) instance->has_VK_ARM_tensors = true;
}

void check_vkCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo)
{
	instance->has_VK_KHR_synchronization2 = true;
	check_vkCmdPipelineBarrier2(commandBuffer, pDependencyInfo);
}

void check_vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query)
{
	instance->core13.synchronization2 = true;
	if (uses_pre13_synchronization2()) instance->has_VK_KHR_synchronization2 = true;
	if (uses_ray_tracing_maintenance1_stage(stage)) instance->has_VK_KHR_ray_tracing_maintenance1 = true;
	if (uses_opacity_micromap_stage(stage)) instance->has_VK_EXT_opacity_micromap = true;
}

void check_vkCmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query)
{
	instance->has_VK_KHR_synchronization2 = true;
	check_vkCmdWriteTimestamp2(commandBuffer, stage, queryPool, query);
}

VkResult check_vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence)
{
	instance->core13.synchronization2 = true;
	if (uses_pre13_synchronization2()) instance->has_VK_KHR_synchronization2 = true;
	assert(submitCount == 0 || pSubmits != nullptr);
	for (uint32_t i = 0; i < submitCount; i++)
	{
		if (submit_info_uses_ray_tracing_maintenance1(&pSubmits[i])) instance->has_VK_KHR_ray_tracing_maintenance1 = true;
		for (uint32_t j = 0; j < pSubmits[i].waitSemaphoreInfoCount; j++)
		{
			if (uses_opacity_micromap_stage(pSubmits[i].pWaitSemaphoreInfos[j].stageMask)) instance->has_VK_EXT_opacity_micromap = true;
		}
		for (uint32_t j = 0; j < pSubmits[i].signalSemaphoreInfoCount; j++)
		{
			if (uses_opacity_micromap_stage(pSubmits[i].pSignalSemaphoreInfos[j].stageMask)) instance->has_VK_EXT_opacity_micromap = true;
		}
		if (submit_pnext_uses_tensors(pSubmits[i].pNext)) instance->has_VK_ARM_tensors = true;
	}
	return VK_SUCCESS;
}

VkResult check_vkQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence)
{
	instance->has_VK_KHR_synchronization2 = true;
	return check_vkQueueSubmit2(queue, submitCount, pSubmits, fence);
}

VkResult check_vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos)
{
	return VK_SUCCESS;
}

VkResult check_vkBindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos)
{
	instance->has_VK_KHR_bind_memory2 = true;
	return VK_SUCCESS;
}

VkResult check_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos)
{
	return VK_SUCCESS;
}

VkResult check_vkBindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos)
{
	instance->has_VK_KHR_bind_memory2 = true;
	return VK_SUCCESS;
}

void check_vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
{
	assert(descriptorWriteCount == 0 || pDescriptorWrites != nullptr);
	for (uint32_t i = 0; i < descriptorWriteCount; i++)
	{
		if (pDescriptorWrites[i].descriptorType == VK_DESCRIPTOR_TYPE_TENSOR_ARM ||
		    get_extension(&pDescriptorWrites[i], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_TENSOR_ARM))
		{
			instance->has_VK_ARM_tensors = true;
		}
	}
}

void check_vkGetDescriptorEXT(VkDevice device, const VkDescriptorGetInfoEXT* pDescriptorInfo, size_t dataSize, void* pDescriptor)
{
	if (pDescriptorInfo && (pDescriptorInfo->type == VK_DESCRIPTOR_TYPE_TENSOR_ARM ||
	                        get_extension(pDescriptorInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_GET_TENSOR_INFO_ARM)))
	{
		instance->has_VK_ARM_tensors = true;
	}
}

VkResult check_vkCreateTensorARM(VkDevice device, const VkTensorCreateInfoARM* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkTensorARM* pTensor)
{
	if (pCreateInfo)
	{
		instance->has_VK_ARM_tensors = true;
		if (get_extension(pCreateInfo, VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_TENSOR_CREATE_INFO_ARM)) instance->has_VK_ARM_tensors = true;
	}
	return VK_SUCCESS;
}

VkResult check_vkCreateTensorViewARM(VkDevice device, const VkTensorViewCreateInfoARM* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkTensorViewARM* pView)
{
	if (pCreateInfo) instance->has_VK_ARM_tensors = true;
	return VK_SUCCESS;
}

void check_vkGetTensorMemoryRequirementsARM(VkDevice device, const VkTensorMemoryRequirementsInfoARM* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
	if (pInfo) instance->has_VK_ARM_tensors = true;
}

VkResult check_vkBindTensorMemoryARM(VkDevice device, uint32_t bindInfoCount, const VkBindTensorMemoryInfoARM* pBindInfos)
{
	if (bindInfoCount > 0) instance->has_VK_ARM_tensors = true;
	return VK_SUCCESS;
}

void check_vkGetDeviceTensorMemoryRequirementsARM(VkDevice device, const VkDeviceTensorMemoryRequirementsARM* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
	if (pInfo) instance->has_VK_ARM_tensors = true;
}

void check_vkCmdCopyTensorARM(VkCommandBuffer commandBuffer, const VkCopyTensorInfoARM* pCopyTensorInfo)
{
	if (pCopyTensorInfo) instance->has_VK_ARM_tensors = true;
}

void check_vkGetPhysicalDeviceExternalTensorPropertiesARM(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalTensorInfoARM* pExternalTensorInfo, VkExternalTensorPropertiesARM* pExternalTensorProperties)
{
	if (pExternalTensorInfo) instance->has_VK_ARM_tensors = true;
}

VkResult check_vkGetTensorOpaqueCaptureDescriptorDataARM(VkDevice device, const VkTensorCaptureDescriptorDataInfoARM* pInfo, void* pData)
{
	if (pInfo) instance->has_VK_ARM_tensors = true;
	return VK_SUCCESS;
}

VkResult check_vkGetTensorViewOpaqueCaptureDescriptorDataARM(VkDevice device, const VkTensorViewCaptureDescriptorDataInfoARM* pInfo, void* pData)
{
	if (pInfo) instance->has_VK_ARM_tensors = true;
	return VK_SUCCESS;
}

void check_vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers,
                                                const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes)
{
	instance->has_VK_EXT_transform_feedback = true;
}

void check_vkCmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount,
                                          const VkBuffer* pCounterBuffers, const VkDeviceSize* pCounterBufferOffsets)
{
	instance->has_VK_EXT_transform_feedback = true;
}

void check_vkCmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount,
                                        const VkBuffer* pCounterBuffers, const VkDeviceSize* pCounterBufferOffsets)
{
	instance->has_VK_EXT_transform_feedback = true;
}

void check_vkCmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags, uint32_t index)
{
	instance->has_VK_EXT_transform_feedback = true;
}

void check_vkCmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, uint32_t index)
{
	instance->has_VK_EXT_transform_feedback = true;
}

void check_vkCmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance, VkBuffer counterBuffer,
                                         VkDeviceSize counterBufferOffset, uint32_t counterOffset, uint32_t vertexStride)
{
	instance->has_VK_EXT_transform_feedback = true;
}

void check_vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo)
{
}

void check_vkCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo)
{
	instance->has_VK_KHR_copy_commands2 = true;
}

void check_vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo)
{
}

void check_vkCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo)
{
	instance->has_VK_KHR_copy_commands2 = true;
}

void check_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo)
{
}

void check_vkCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo)
{
	instance->has_VK_KHR_copy_commands2 = true;
}

void check_vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo)
{
}

void check_vkCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo)
{
	instance->has_VK_KHR_copy_commands2 = true;
}

void check_vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo)
{
}

void check_vkCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo)
{
	instance->has_VK_KHR_copy_commands2 = true;
}

void check_vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo)
{
}

void check_vkCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo)
{
	instance->has_VK_KHR_copy_commands2 = true;
}

void check_vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
}

void check_vkGetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
	instance->has_VK_KHR_get_memory_requirements2 = true;
}

void check_vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
}

void check_vkGetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
	instance->has_VK_KHR_get_memory_requirements2 = true;
}

void check_vkGetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
{
}

void check_vkGetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
{
	instance->has_VK_KHR_get_memory_requirements2 = true;
}

VkResult check_vkMapMemory2(VkDevice device, const VkMemoryMapInfo* pMemoryMapInfo, void** ppData)
{
	instance->has_VK_KHR_map_memory2 = true;
	return VK_SUCCESS;
}

VkResult check_vkMapMemory2KHR(VkDevice device, const VkMemoryMapInfo* pMemoryMapInfo, void** ppData)
{
	instance->has_VK_KHR_map_memory2 = true;
	return VK_SUCCESS;
}

VkResult check_vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo* pMemoryUnmapInfo)
{
	instance->has_VK_KHR_map_memory2 = true;
	return VK_SUCCESS;
}

VkResult check_vkUnmapMemory2KHR(VkDevice device, const VkMemoryUnmapInfo* pMemoryUnmapInfo)
{
	instance->has_VK_KHR_map_memory2 = true;
	return VK_SUCCESS;
}

void check_vkTrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags)
{
	instance->has_VK_KHR_maintenance1 = true;
}

VkResult check_vkGetMemoryHostPointerPropertiesEXT(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, const void* pHostPointer, VkMemoryHostPointerPropertiesEXT* pMemoryHostPointerProperties)
{
	instance->has_VK_EXT_external_memory_host = true;
	return VK_SUCCESS;
}

// Note that this is place where the Vulkan standard gets really awful. pInheritanceInfo is allowed to be a garbage invalid pointer
// if commandBuffer is a primary rather than secondary command buffer, and we have no way of knowing which by simply inspecting command
// inputs. So we have to special case this one and require an extra parameter.
void special_vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo, VkCommandBufferLevel level)
{
	if (level == VK_COMMAND_BUFFER_LEVEL_SECONDARY && pBeginInfo->pInheritanceInfo)
	{
		const VkCommandBufferInheritanceRenderingInfo* rendering_info =
			(const VkCommandBufferInheritanceRenderingInfo*)get_extension(
				pBeginInfo->pInheritanceInfo->pNext, VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO);
		if (rendering_info)
		{
			instance->core13.dynamicRendering = true;
			if (uses_pre13_dynamic_rendering()) instance->has_VK_KHR_dynamic_rendering = true;
		}
		if (pBeginInfo->pInheritanceInfo->occlusionQueryEnable != VK_FALSE || ((pBeginInfo->pInheritanceInfo->queryFlags & ~(VK_QUERY_CONTROL_PRECISE_BIT)) == 0))
		{
			instance->core10.inheritedQueries = true;
		}
	}
}

VkDeviceAddress check_vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo)
{
	instance->core12.bufferDeviceAddress = true;
	return 0;
}

VkDeviceAddress check_vkGetBufferDeviceAddressKHR(VkDevice device, const VkBufferDeviceAddressInfoKHR* pInfo)
{
	instance->core12.bufferDeviceAddress = true;
	return 0;
}

VkDeviceAddress check_vkGetBufferDeviceAddressEXT(VkDevice device, const VkBufferDeviceAddressInfoEXT* pInfo)
{
	instance->core12.bufferDeviceAddress = true;
	return 0;
}

uint64_t check_vkGetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo)
{
	instance->core12.bufferDeviceAddressCaptureReplay = true;
	return 0;
}

uint64_t check_vkGetBufferOpaqueCaptureAddressKHR(VkDevice device, const VkBufferDeviceAddressInfoKHR* pInfo)
{
	instance->core12.bufferDeviceAddressCaptureReplay = true;
	return 0;
}

uint64_t check_vkGetBufferOpaqueCaptureAddressEXT(VkDevice device, const VkBufferDeviceAddressInfoEXT* pInfo)
{
	instance->core12.bufferDeviceAddressCaptureReplay = true;
	return 0;
}

void check_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
	if (lineWidth != 1.0) instance->core10.wideLines = true;
}

void check_vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern)
{
	mark_all_stippled_line_features_used();
}

void check_vkCmdSetLineStippleKHR(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern)
{
	mark_all_stippled_line_features_used();
}

void check_vkCmdSetLineStippleEXT(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern)
{
	mark_all_stippled_line_features_used();
}

void check_vkCmdSetLineRasterizationModeEXT(VkCommandBuffer commandBuffer, VkLineRasterizationModeEXT lineRasterizationMode)
{
	mark_line_rasterization_mode_usage((VkLineRasterizationMode)lineRasterizationMode, VK_FALSE);
}

void check_vkCmdSetLineStippleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stippledLineEnable)
{
	if (stippledLineEnable == VK_TRUE) mark_all_stippled_line_features_used();
}

void check_vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
{
	if (depthBiasClamp != 0.0) instance->core10.depthBiasClamp = true;
}

void check_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	if (drawCount > 1) instance->core10.multiDrawIndirect = true;
}

void check_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	if (drawCount > 1) instance->core10.multiDrawIndirect = true;
}

void check_vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
{
	if (flags & VK_QUERY_CONTROL_PRECISE_BIT) instance->core10.occlusionQueryPrecise = true;
}

void check_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
	instance->core12.drawIndirectCount = true;
}

void check_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
	if (indexType == VK_INDEX_TYPE_UINT32) instance->core10.fullDrawIndexUint32 = true; // defensive assumption
	if (indexType == VK_INDEX_TYPE_UINT8) instance->core14.indexTypeUint8 = true;
}

void check_vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType)
{
	if (indexType == VK_INDEX_TYPE_UINT32) instance->core10.fullDrawIndexUint32 = true; // defensive assumption
	if (indexType == VK_INDEX_TYPE_UINT8) instance->core14.indexTypeUint8 = true;
}

void check_vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
	instance->core12.drawIndirectCount = true;
}

void check_vkResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
	instance->core12.hostQueryReset = true;
}

void check_vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo)
{
	instance->core13.dynamicRendering = true;
	if (uses_pre13_dynamic_rendering()) instance->has_VK_KHR_dynamic_rendering = true;
	if (pRenderingInfo->viewMask != 0) instance->core11.multiview = true;
}

void check_vkCmdBeginRenderingKHR(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo)
{
	instance->has_VK_KHR_dynamic_rendering = true;
	check_vkCmdBeginRendering(commandBuffer, pRenderingInfo);
}

void check_vkCmdEndRenderingKHR(VkCommandBuffer commandBuffer)
{
	instance->has_VK_KHR_dynamic_rendering = true;
}

void check_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports)
{
	assert(viewportCount == 0 || pViewports != nullptr);
	if (firstViewport != 0 || viewportCount != 1)
	{
		instance->core10.multiViewport = true;
	}
	for (uint32_t i = 0; i < viewportCount; i++)
	{
		if (pViewports[i].height < 0.0f) instance->has_VK_KHR_maintenance1 = true;
	}
}

void check_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors)
{
	if (firstScissor != 0 || scissorCount != 1)
	{
		instance->core10.multiViewport = true;
	}
}

void check_vkCmdSetExclusiveScissorNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkRect2D* pExclusiveScissors)
{
	if (firstExclusiveScissor != 0 || exclusiveScissorCount != 1)
	{
		instance->core10.multiViewport = true;
	}
}
