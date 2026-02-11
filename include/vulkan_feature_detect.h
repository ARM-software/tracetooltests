#pragma once

#include <atomic>
#include <string>
#include <unordered_set>
#include "vulkan/vulkan.h"

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
//
// You should not call the check function if the parent call failed.

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

struct atomicPhysicalDeviceVulkan14Features // most are not handled
{
	std::atomic_bool globalPriorityQuery { false };
	std::atomic_bool shaderSubgroupRotate { false };
	std::atomic_bool shaderSubgroupRotateClustered { false };
	std::atomic_bool shaderFloatControls2 { false };
	std::atomic_bool shaderExpectAssume { false };
	std::atomic_bool rectangularLines { false };
	std::atomic_bool bresenhamLines { false };
	std::atomic_bool smoothLines { false };
	std::atomic_bool stippledRectangularLines { false };
	std::atomic_bool stippledBresenhamLines { false };
	std::atomic_bool stippledSmoothLines { false };
	std::atomic_bool vertexAttributeInstanceRateDivisor { false };
	std::atomic_bool vertexAttributeInstanceRateZeroDivisor { false };
	std::atomic_bool indexTypeUint8 { false };
	std::atomic_bool dynamicRenderingLocalRead { false };
	std::atomic_bool maintenance5 { false };
	std::atomic_bool maintenance6 { false };
	std::atomic_bool pipelineProtectedAccess { false };
	std::atomic_bool pipelineRobustness { false };
	std::atomic_bool hostImageCopy { false };
	std::atomic_bool pushDescriptor { false };
};

struct feature_detection
{
	// Features
	struct atomicPhysicalDeviceFeatures core10;
	struct atomicPhysicalDeviceVulkan11Features core11;
	struct atomicPhysicalDeviceVulkan12Features core12;
	struct atomicPhysicalDeviceVulkan13Features core13;
	struct atomicPhysicalDeviceVulkan14Features core14;

	// Extensions
	std::atomic_bool has_VK_EXT_swapchain_colorspace { false };
	std::atomic_bool has_VkPhysicalDeviceShaderAtomicInt64Features { false };
	std::atomic_bool has_VK_KHR_shared_presentable_image { false };
	std::atomic_bool has_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT { false };
	std::atomic_bool has_VK_IMG_filter_cubic { false };
	std::atomic_bool has_VK_EXT_shader_viewport_index_layer { false };

	// --- Remove unused feature bits from these structures ---
	std::unordered_set<std::string> adjust_VkDeviceCreateInfo(VkDeviceCreateInfo* info, const std::unordered_set<std::string>& exts) const;
	std::unordered_set<std::string> adjust_VkInstanceCreateInfo(VkInstanceCreateInfo* info, const std::unordered_set<std::string>& removed) const;
	std::unordered_set<std::string> adjust_device_extensions(std::unordered_set<std::string>& exts) const;
	std::unordered_set<std::string> adjust_instance_extensions(std::unordered_set<std::string>& exts) const;
	std::unordered_set<std::string> adjust_VkPhysicalDeviceFeatures(VkPhysicalDeviceFeatures& incore10) const;
	std::unordered_set<std::string> adjust_VkPhysicalDeviceVulkan11Features(VkPhysicalDeviceVulkan11Features& incore11) const;
	std::unordered_set<std::string> adjust_VkPhysicalDeviceVulkan12Features(VkPhysicalDeviceVulkan12Features& incore12) const;
	std::unordered_set<std::string> adjust_VkPhysicalDeviceVulkan13Features(VkPhysicalDeviceVulkan13Features& incore13) const;
	std::unordered_set<std::string> adjust_VkPhysicalDeviceVulkan14Features(VkPhysicalDeviceVulkan14Features& incore14) const;
};

// --- Setup functions ---

// Make sure you call this once before any of the other functions to create the instance.
feature_detection* vulkan_feature_detection_get();

// Clear out old data
void vulkan_feature_detection_reset();

// --- Checking functions. Call these for all these Vulkan commands after they are successfully called, before returning. ---

// These check_* functions have identical function definition as their Vulkan equivalent.

VkResult check_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule);
VkResult check_vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);
VkResult check_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
VkResult check_vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
VkResult check_vkCreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
VkResult check_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
VkResult check_vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities);
VkResult check_vkCreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains);
VkResult check_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
VkResult check_vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler);
void check_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter);
VkResult vkCreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion);
VkResult vkCreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion);
VkResult check_vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool);
VkResult check_vkCreateImage(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkImage* pImage);
VkResult check_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer);
VkResult check_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkImageView* pView);
VkDeviceAddress check_vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
VkDeviceAddress check_vkGetBufferDeviceAddressEXT(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
VkDeviceAddress check_vkGetBufferDeviceAddressKHR(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
uint64_t check_vkGetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
uint64_t check_vkGetBufferOpaqueCaptureAddressEXT(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
uint64_t check_vkGetBufferOpaqueCaptureAddressKHR(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
void check_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth);
void check_vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor);
void check_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
void check_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
void check_vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags);
void check_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride);
void check_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
void check_vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType);
void check_vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride);
void check_vkResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
void check_vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo);
void check_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports);
void check_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors);
void check_vkCmdSetExclusiveScissorNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkRect2D* pExclusiveScissors);

// Note that this is place where the Vulkan standard gets really awful. pInheritanceInfo is allowed to be a garbage invalid pointer
// if commandBuffer is a primary rather than secondary command buffer, and we have no way of knowing which by simply inspecting command
// inputs. So we have to special case this one and require an extra parameter.
void special_vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo, VkCommandBufferLevel level);

// --- These helper functions are provided here just for ease of testing. No need to call them directly ---
// TBD Remove them from this header

void struct_check_VkPipelineShaderStageCreateInfo(const VkPipelineShaderStageCreateInfo* info);
void struct_check_VkPipelineColorBlendAttachmentState(const VkPipelineColorBlendAttachmentState* info);
void struct_check_VkPipelineColorBlendStateCreateInfo(const VkPipelineColorBlendStateCreateInfo* info);
void struct_check_VkPipelineMultisampleStateCreateInfo(const VkPipelineMultisampleStateCreateInfo* info);
void struct_check_VkPipelineRasterizationStateCreateInfo(const VkPipelineRasterizationStateCreateInfo* info);
void struct_check_VkPipelineDepthStencilStateCreateInfo(const VkPipelineDepthStencilStateCreateInfo* info);
void struct_check_VkPipelineViewportStateCreateInfo(const VkPipelineViewportStateCreateInfo* info);
