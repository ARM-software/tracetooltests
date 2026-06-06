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
	std::atomic_bool largePoints { false };
	std::atomic_bool alphaToOne { false };
	std::atomic_bool multiViewport { false };
	std::atomic_bool samplerAnisotropy { false };
	std::atomic_bool textureCompressionETC2 { false };
	std::atomic_bool textureCompressionASTC_LDR { false };
	std::atomic_bool textureCompressionBC { false };
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
	std::atomic_bool multiview { false };
	std::atomic_bool multiviewGeometryShader { false };
	std::atomic_bool multiviewTessellationShader { false };
	std::atomic_bool variablePointersStorageBuffer { false };
	std::atomic_bool variablePointers { false };
	std::atomic_bool protectedMemory { false }; // not handled
	std::atomic_bool samplerYcbcrConversion { false }; // not handled
	std::atomic_bool shaderDrawParameters { false };
};

struct atomicPhysicalDeviceVulkan12Features
{
	std::atomic_bool samplerMirrorClampToEdge { false };
	std::atomic_bool drawIndirectCount { false };
	std::atomic_bool storageBuffer8BitAccess { false };
	std::atomic_bool uniformAndStorageBuffer8BitAccess { false };
	std::atomic_bool storagePushConstant8 { false };
	std::atomic_bool shaderBufferInt64Atomics { false }; // not handled
	std::atomic_bool shaderSharedInt64Atomics { false }; // not handled
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
	std::atomic_bool samplerFilterMinmax { false }; // not handled
	std::atomic_bool scalarBlockLayout { false }; // not handled
	std::atomic_bool imagelessFramebuffer { false }; // not handled
	std::atomic_bool uniformBufferStandardLayout { false }; // not handled
	std::atomic_bool shaderSubgroupExtendedTypes { false }; // not handled
	std::atomic_bool separateDepthStencilLayouts { false }; // not handled
	std::atomic_bool hostQueryReset { false };
	std::atomic_bool timelineSemaphore { false };
	std::atomic_bool bufferDeviceAddress { false };
	std::atomic_bool bufferDeviceAddressCaptureReplay { false };
	std::atomic_bool bufferDeviceAddressMultiDevice { false };
	std::atomic_bool vulkanMemoryModel { false };
	std::atomic_bool vulkanMemoryModelDeviceScope { false };
	std::atomic_bool vulkanMemoryModelAvailabilityVisibilityChains { false }; // not handled
	std::atomic_bool shaderOutputViewportIndex { false };
	std::atomic_bool shaderOutputLayer { false };
	std::atomic_bool subgroupBroadcastDynamicId { false }; // not handled
};

struct atomicPhysicalDeviceVulkan13Features
{
	std::atomic_bool robustImageAccess { false }; // not handled
	std::atomic_bool inlineUniformBlock { false }; // not handled
	std::atomic_bool descriptorBindingInlineUniformBlockUpdateAfterBind { false }; // not handled
	std::atomic_bool pipelineCreationCacheControl { false }; // not handled
	std::atomic_bool privateData { false }; // not handled
	std::atomic_bool shaderDemoteToHelperInvocation { false };
	std::atomic_bool shaderTerminateInvocation { false }; // not handled
	std::atomic_bool subgroupSizeControl { false };
	std::atomic_bool computeFullSubgroups { false }; // not handled
	std::atomic_bool synchronization2 { false };
	std::atomic_bool textureCompressionASTC_HDR { false }; // not handled
	std::atomic_bool shaderZeroInitializeWorkgroupMemory { false }; // not handled
	std::atomic_bool dynamicRendering { false };
	std::atomic_bool shaderIntegerDotProduct { false };
	std::atomic_bool maintenance4 { false }; // not handled
};

struct atomicPhysicalDeviceVulkan14Features
{
	std::atomic_bool globalPriorityQuery { false }; // not handled
	std::atomic_bool shaderSubgroupRotate { false };
	std::atomic_bool shaderSubgroupRotateClustered { false }; // not handled
	std::atomic_bool shaderFloatControls2 { false };
	std::atomic_bool shaderExpectAssume { false };
	std::atomic_bool rectangularLines { false };
	std::atomic_bool bresenhamLines { false };
	std::atomic_bool smoothLines { false };
	std::atomic_bool stippledRectangularLines { false };
	std::atomic_bool stippledBresenhamLines { false };
	std::atomic_bool stippledSmoothLines { false };
	std::atomic_bool vertexAttributeInstanceRateDivisor { false }; // not handled
	std::atomic_bool vertexAttributeInstanceRateZeroDivisor { false }; // not handled
	std::atomic_bool indexTypeUint8 { false };
	std::atomic_bool dynamicRenderingLocalRead { false }; // not handled
	std::atomic_bool maintenance5 { false }; // not handled
	std::atomic_bool maintenance6 { false }; // not handled
	std::atomic_bool pipelineProtectedAccess { false }; // not handled
	std::atomic_bool pipelineRobustness { false }; // not handled
	std::atomic_bool hostImageCopy { false }; // not handled
	std::atomic_bool pushDescriptor { false }; // not handled
};

struct feature_detection
{
	// Features
	struct atomicPhysicalDeviceFeatures core10;
	struct atomicPhysicalDeviceVulkan11Features core11;
	struct atomicPhysicalDeviceVulkan12Features core12;
	struct atomicPhysicalDeviceVulkan13Features core13;
	struct atomicPhysicalDeviceVulkan14Features core14;
	std::atomic_uint requested_instance_api_version { VK_API_VERSION_1_0 };

	// Extensions
	std::atomic_bool has_VK_EXT_swapchain_colorspace { false };
	std::atomic_bool has_VK_KHR_get_physical_device_properties2 { false };
	std::atomic_bool has_VK_KHR_external_fence_capabilities { false };
	std::atomic_bool has_VkPhysicalDeviceShaderAtomicInt64Features { false };
	std::atomic_bool has_VK_KHR_shared_presentable_image { false };
	std::atomic_bool has_VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT { false };
	std::atomic_bool has_VK_EXT_external_memory_host { false };
	std::atomic_bool has_VK_IMG_filter_cubic { false };
	std::atomic_bool has_VK_KHR_bind_memory2 { false };
	std::atomic_bool has_VK_KHR_create_renderpass2 { false };
	std::atomic_bool has_VK_KHR_copy_commands2 { false };
	std::atomic_bool has_VK_KHR_dynamic_rendering { false };
	std::atomic_bool has_VK_KHR_get_memory_requirements2 { false };
	std::atomic_bool has_VK_KHR_maintenance1 { false };
	std::atomic_bool has_VK_KHR_external_memory { false };
	std::atomic_bool has_VK_KHR_external_memory_fd { false };
	std::atomic_bool has_VK_KHR_map_memory2 { false };
	std::atomic_bool has_VK_KHR_multiview { false };
	std::atomic_bool has_VK_KHR_synchronization2 { false };
	std::atomic_bool has_VK_KHR_acceleration_structure { false };
	std::atomic_bool has_VK_KHR_ray_query { false };
	std::atomic_bool has_VK_ARM_shader_core_properties { false };
	std::atomic_bool has_VK_ARM_tensors { false };
	std::atomic_bool has_VK_KHR_ray_tracing_pipeline { false };
	std::atomic_bool has_VK_KHR_ray_tracing_maintenance1 { false };
	std::atomic_bool has_VK_KHR_robustness2 { false };
	std::atomic_bool has_VK_EXT_descriptor_heap { false };
	std::atomic_bool has_VK_EXT_opacity_micromap { false };
	std::atomic_bool has_VK_ARM_pipeline_opacity_micromap { false };
	std::atomic_bool has_VK_EXT_robustness2 { false };
	std::atomic_bool has_VK_EXT_shader_viewport_index_layer { false };
	std::atomic_bool has_VK_EXT_transform_feedback { false };
	std::atomic_bool has_VK_EXT_descriptor_indexing { false };

	// --- Remove unused feature bits from these structures ---
	std::unordered_set<std::string> adjust_VkDeviceCreateInfo(VkDeviceCreateInfo* info, const std::unordered_set<std::string>& enabled_exts) const;
	std::unordered_set<std::string> adjust_VkInstanceCreateInfo(VkInstanceCreateInfo* info, const std::unordered_set<std::string>& enabled_exts) const;
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

VkResult check_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
VkResult check_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule);
VkResult check_vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);
VkResult check_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
VkResult check_vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
VkResult check_vkCreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
VkResult check_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
VkResult check_vkCreateMicromapEXT(VkDevice device, const VkMicromapCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkMicromapEXT* pMicromap);
void check_vkDestroyMicromapEXT(VkDevice device, VkMicromapEXT micromap, const VkAllocationCallbacks* pAllocator);
VkResult check_vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
VkResult check_vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
VkResult check_vkCreateRenderPass2KHR(VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
void check_vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo);
void check_vkCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo);
void check_vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo);
void check_vkCmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo);
void check_vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo);
void check_vkCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo);
void check_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures);
void check_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties);
void check_vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties);
void check_vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties);
VkResult check_vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
                                                            VkImageFormatProperties2* pImageFormatProperties);
void check_vkGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount,
                                                        VkQueueFamilyProperties2* pQueueFamilyProperties);
void check_vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties);
void check_vkGetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
                                                              uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties);
void check_vkGetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo,
                                                         VkExternalFenceProperties* pExternalFenceProperties);
VkResult check_vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities);
VkResult check_vkCreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains);
VkResult check_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
VkResult check_vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler);
VkResult check_vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                           VkDescriptorSetLayout* pSetLayout);
VkResult check_vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                      VkDescriptorPool* pDescriptorPool);
VkResult check_vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets);
void check_vkGetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport);
void check_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter);
VkResult vkCreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion);
VkResult vkCreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion);
VkResult check_vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool);
VkResult check_vkCreateIndirectCommandsLayoutEXT(VkDevice device, const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                                 VkIndirectCommandsLayoutEXT* pIndirectCommandsLayout);
VkResult check_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);
VkResult check_vkCreateImage(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkImage* pImage);
VkResult check_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer);
VkResult check_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);
VkResult check_vkGetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd);
VkResult check_vkGetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties);
VkResult check_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* info, const VkAllocationCallbacks* pAllocator, VkImageView* pView);
VkResult check_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
VkResult check_vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence);
VkResult check_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
void check_vkGetMicromapBuildSizesEXT(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkMicromapBuildInfoEXT* pBuildInfo,
                                      VkMicromapBuildSizesInfoEXT* pSizeInfo);
VkResult check_vkBuildMicromapsEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount, const VkMicromapBuildInfoEXT* pInfos);
void check_vkCmdBuildMicromapsEXT(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkMicromapBuildInfoEXT* pInfos);
VkResult check_vkCopyMicromapEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMicromapInfoEXT* pInfo);
VkResult check_vkCopyMicromapToMemoryEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMicromapToMemoryInfoEXT* pInfo);
VkResult check_vkCopyMemoryToMicromapEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMemoryToMicromapInfoEXT* pInfo);
VkResult check_vkWriteMicromapsPropertiesEXT(VkDevice device, uint32_t micromapCount, const VkMicromapEXT* pMicromaps, VkQueryType queryType, size_t dataSize,
                                             void* pData, size_t stride);
void check_vkCmdCopyMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapInfoEXT* pInfo);
void check_vkCmdCopyMicromapToMemoryEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapToMemoryInfoEXT* pInfo);
void check_vkCmdCopyMemoryToMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMemoryToMicromapInfoEXT* pInfo);
void check_vkCmdWriteMicromapsPropertiesEXT(VkCommandBuffer commandBuffer, uint32_t micromapCount, const VkMicromapEXT* pMicromaps, VkQueryType queryType,
                                            VkQueryPool queryPool, uint32_t firstQuery);
void check_vkGetDeviceMicromapCompatibilityEXT(VkDevice device, const VkMicromapVersionInfoEXT* pVersionInfo,
                                               VkAccelerationStructureCompatibilityKHR* pCompatibility);
void check_vkGetAccelerationStructureBuildSizesKHR(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType,
                                                   const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo, const uint32_t* pMaxPrimitiveCounts,
                                                   VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo);
VkResult check_vkBuildAccelerationStructuresKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount,
                                                const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
                                                const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);
void check_vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                               const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
                                               const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);
VkResult check_vkGetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize,
                                                    void* pData);
VkResult check_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount,
                                                                 size_t dataSize, void* pData);
void check_vkCmdTraceRaysKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
                             const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
                             const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
                             const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth);
void check_vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
                                     const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
                                     const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
                                     const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress);
VkDeviceSize check_vkGetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline pipeline, uint32_t group, VkShaderGroupShaderKHR groupShader);
void check_vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize);
void check_vkCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress);
void check_vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo);
void check_vkCmdSetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo);
void check_vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask);
void check_vkCmdResetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask);
void check_vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos);
void check_vkCmdWaitEvents2KHR(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos);
void check_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo);
void check_vkCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo);
void check_vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query);
void check_vkCmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query);
VkResult check_vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence);
VkResult check_vkQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence);
VkResult check_vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos);
VkResult check_vkBindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos);
VkResult check_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos);
VkResult check_vkBindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos);
void check_vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies);
void check_vkGetDescriptorEXT(VkDevice device, const VkDescriptorGetInfoEXT* pDescriptorInfo, size_t dataSize, void* pDescriptor);
VkResult check_vkCreateTensorARM(VkDevice device, const VkTensorCreateInfoARM* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkTensorARM* pTensor);
VkResult check_vkCreateTensorViewARM(VkDevice device, const VkTensorViewCreateInfoARM* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkTensorViewARM* pView);
void check_vkGetTensorMemoryRequirementsARM(VkDevice device, const VkTensorMemoryRequirementsInfoARM* pInfo, VkMemoryRequirements2* pMemoryRequirements);
VkResult check_vkBindTensorMemoryARM(VkDevice device, uint32_t bindInfoCount, const VkBindTensorMemoryInfoARM* pBindInfos);
void check_vkGetDeviceTensorMemoryRequirementsARM(VkDevice device, const VkDeviceTensorMemoryRequirementsARM* pInfo, VkMemoryRequirements2* pMemoryRequirements);
void check_vkCmdCopyTensorARM(VkCommandBuffer commandBuffer, const VkCopyTensorInfoARM* pCopyTensorInfo);
void check_vkGetPhysicalDeviceExternalTensorPropertiesARM(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalTensorInfoARM* pExternalTensorInfo, VkExternalTensorPropertiesARM* pExternalTensorProperties);
VkResult check_vkGetTensorOpaqueCaptureDescriptorDataARM(VkDevice device, const VkTensorCaptureDescriptorDataInfoARM* pInfo, void* pData);
VkResult check_vkGetTensorViewOpaqueCaptureDescriptorDataARM(VkDevice device, const VkTensorViewCaptureDescriptorDataInfoARM* pInfo, void* pData);
void check_vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers,
                                                const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes);
void check_vkCmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount,
                                          const VkBuffer* pCounterBuffers, const VkDeviceSize* pCounterBufferOffsets);
void check_vkCmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount,
                                        const VkBuffer* pCounterBuffers, const VkDeviceSize* pCounterBufferOffsets);
void check_vkCmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags, uint32_t index);
void check_vkCmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, uint32_t index);
void check_vkCmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance, VkBuffer counterBuffer,
                                         VkDeviceSize counterBufferOffset, uint32_t counterOffset, uint32_t vertexStride);
void check_vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo);
void check_vkCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo);
void check_vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo);
void check_vkCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo);
void check_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo);
void check_vkCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo);
void check_vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo);
void check_vkCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo);
void check_vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo);
void check_vkCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo);
void check_vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo);
void check_vkCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo);
void check_vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements);
void check_vkGetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements);
void check_vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements);
void check_vkGetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements);
void check_vkGetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements);
void check_vkGetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements);
VkResult check_vkMapMemory2(VkDevice device, const VkMemoryMapInfo* pMemoryMapInfo, void** ppData);
VkResult check_vkMapMemory2KHR(VkDevice device, const VkMemoryMapInfo* pMemoryMapInfo, void** ppData);
VkResult check_vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo* pMemoryUnmapInfo);
VkResult check_vkUnmapMemory2KHR(VkDevice device, const VkMemoryUnmapInfo* pMemoryUnmapInfo);
void check_vkTrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags);
VkResult check_vkGetMemoryHostPointerPropertiesEXT(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, const void* pHostPointer, VkMemoryHostPointerPropertiesEXT* pMemoryHostPointerProperties);
VkDeviceAddress check_vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
VkDeviceAddress check_vkGetBufferDeviceAddressEXT(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
VkDeviceAddress check_vkGetBufferDeviceAddressKHR(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
uint64_t check_vkGetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
uint64_t check_vkGetBufferOpaqueCaptureAddressEXT(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
uint64_t check_vkGetBufferOpaqueCaptureAddressKHR(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
void check_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth);
void check_vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern);
void check_vkCmdSetLineStippleKHR(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern);
void check_vkCmdSetLineStippleEXT(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern);
void check_vkCmdSetLineRasterizationModeEXT(VkCommandBuffer commandBuffer, VkLineRasterizationModeEXT lineRasterizationMode);
void check_vkCmdSetLineStippleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stippledLineEnable);
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
void check_vkCmdBeginRenderingKHR(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo);
void check_vkCmdEndRenderingKHR(VkCommandBuffer commandBuffer);
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
