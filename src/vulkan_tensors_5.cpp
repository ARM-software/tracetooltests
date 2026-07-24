#include "vulkan_common.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "vulkan_tensors_2_preprocessing.inc"

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
    assert(alignment != 0);
    const VkDeviceSize remainder = value % alignment;
    if (remainder == 0)
        return value;
    return value + alignment - remainder;
}

static VkShaderModule create_shader_module(const vulkan_setup_t &vulkan, const unsigned char *spirv, uint32_t spirv_len)
{
    const std::vector<uint32_t> code = copy_shader(const_cast<unsigned char *>(spirv), spirv_len);
    VkShaderModuleCreateInfo create_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
    create_info.codeSize = code.size() * sizeof(uint32_t);
    create_info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(vulkan.device, &create_info, nullptr, &module);
    check(result);
    return module;
}

static void submit_frame_boundary(
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer command_buffer,
    VkFence fence,
    uint64_t frame_id,
    VkImage image,
    VkTensorARM tensor,
    uint32_t buffer_count,
    const VkBuffer *buffers)
{
    VkTensorARM tensors[] = {tensor};
    VkFrameBoundaryTensorsARM frame_boundary_tensors = {
        VK_STRUCTURE_TYPE_FRAME_BOUNDARY_TENSORS_ARM,
        nullptr,
        1,
        tensors};
    VkFrameBoundaryEXT frame_boundary = {VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT, &frame_boundary_tensors};
    frame_boundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
    frame_boundary.frameID = frame_id;
    frame_boundary.imageCount = 1;
    frame_boundary.pImages = &image;
    frame_boundary.bufferCount = buffer_count;
    frame_boundary.pBuffers = buffers;

    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO, &frame_boundary};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    VkResult result = vkQueueSubmit(queue, 1, &submit_info, fence);
    check(result);
    result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    check(result);
    result = vkResetFences(device, 1, &fence);
    check(result);
}

int main(int argc, char **argv)
{
    vulkan_req_t reqs;
    VkPhysicalDeviceTensorFeaturesARM tensor_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM, nullptr};
    tensor_features.tensors = VK_TRUE;
    tensor_features.shaderTensorAccess = VK_TRUE;
    VkPhysicalDeviceFrameBoundaryFeaturesEXT frame_boundary_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT, &tensor_features};
    frame_boundary_features.frameBoundary = VK_TRUE;
    reqs.device_extensions.push_back("VK_ARM_tensors");
    reqs.device_extensions.push_back(VK_EXT_FRAME_BOUNDARY_EXTENSION_NAME);
    reqs.options["frame_boundary"] = true;
    reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&frame_boundary_features);
    reqs.minApiVersion = VK_API_VERSION_1_3;
    reqs.apiVersion = VK_API_VERSION_1_3;
    reqs.reqfeat13.synchronization2 = VK_TRUE;

    vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_tensors_5", reqs);

    bench_start_iteration(vulkan.bench);

    MAKEDEVICEPROCADDR(vulkan, vkCreateTensorARM);
    MAKEDEVICEPROCADDR(vulkan, vkDestroyTensorARM);
    MAKEDEVICEPROCADDR(vulkan, vkGetTensorMemoryRequirementsARM);
    MAKEDEVICEPROCADDR(vulkan, vkBindTensorMemoryARM);
    MAKEDEVICEPROCADDR(vulkan, vkCreateTensorViewARM);
    MAKEDEVICEPROCADDR(vulkan, vkDestroyTensorViewARM);

    const VkFormat tensor_format = VK_FORMAT_R32_SFLOAT;
    const VkFormat image_format = VK_FORMAT_R32G32B32_SFLOAT;
    const std::array<int64_t, 4> dimensions = {1, 100, 100, 3};

    VkTensorFormatPropertiesARM tensor_format_properties = {
        VK_STRUCTURE_TYPE_TENSOR_FORMAT_PROPERTIES_ARM,
        nullptr};
    VkFormatProperties2 format_properties = {
        VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        &tensor_format_properties};
    vkGetPhysicalDeviceFormatProperties2(vulkan.physical, tensor_format, &format_properties);
    if ((tensor_format_properties.optimalTilingTensorFeatures & VK_FORMAT_FEATURE_2_TENSOR_SHADER_BIT_ARM) == 0)
    {
        return 77;
    }
    if ((tensor_format_properties.optimalTilingTensorFeatures & VK_FORMAT_FEATURE_2_TENSOR_IMAGE_ALIASING_BIT_ARM) == 0)
    {
        return 77;
    }

    VkTensorDescriptionARM tensor_description = {VK_STRUCTURE_TYPE_TENSOR_DESCRIPTION_ARM, nullptr};
    tensor_description.tiling = VK_TENSOR_TILING_OPTIMAL_ARM;
    tensor_description.format = tensor_format;
    tensor_description.dimensionCount = dimensions.size();
    tensor_description.pDimensions = dimensions.data();
    tensor_description.pStrides = nullptr;
    tensor_description.usage = VK_TENSOR_USAGE_SHADER_BIT_ARM | VK_TENSOR_USAGE_IMAGE_ALIASING_BIT_ARM;

    VkTensorCreateInfoARM tensor_create_info = {VK_STRUCTURE_TYPE_TENSOR_CREATE_INFO_ARM, nullptr};
    tensor_create_info.flags = 0;
    tensor_create_info.pDescription = &tensor_description;
    tensor_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    tensor_create_info.queueFamilyIndexCount = 0;
    tensor_create_info.pQueueFamilyIndices = nullptr;

    VkTensorARM tensor = VK_NULL_HANDLE;
    VkResult result = pf_vkCreateTensorARM(vulkan.device, &tensor_create_info, nullptr, &tensor);
    check(result);

    const uint32_t width = static_cast<uint32_t>(dimensions[2]);
    const uint32_t height = static_cast<uint32_t>(dimensions[1]);
    VkImageCreateInfo image_create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
    image_create_info.flags = 0;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.format = image_format;
    image_create_info.extent = {width, height, 1};
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.usage =
        VK_IMAGE_USAGE_TENSOR_ALIASING_BIT_ARM |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.queueFamilyIndexCount = 0;
    image_create_info.pQueueFamilyIndices = nullptr;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkPhysicalDeviceImageFormatInfo2 image_format_info = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, nullptr};
    image_format_info.format = image_create_info.format;
    image_format_info.type = image_create_info.imageType;
    image_format_info.tiling = image_create_info.tiling;
    image_format_info.usage = image_create_info.usage;
    image_format_info.flags = image_create_info.flags;
    VkImageFormatProperties2 image_format_properties = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, nullptr};
    result = vkGetPhysicalDeviceImageFormatProperties2(vulkan.physical, &image_format_info, &image_format_properties);
    if (result == VK_ERROR_FORMAT_NOT_SUPPORTED)
    {
        pf_vkDestroyTensorARM(vulkan.device, tensor, nullptr);
        return 77;
    }
    check(result);

    VkImage image = VK_NULL_HANDLE;
    result = vkCreateImage(vulkan.device, &image_create_info, nullptr, &image);
    check(result);

    VkTensorMemoryRequirementsInfoARM tensor_requirement_info = {VK_STRUCTURE_TYPE_TENSOR_MEMORY_REQUIREMENTS_INFO_ARM, nullptr};
    tensor_requirement_info.tensor = tensor;
    VkMemoryDedicatedRequirements tensor_dedicated_requirements = {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
        nullptr};
    VkMemoryRequirements2 tensor_requirement = {
        VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        &tensor_dedicated_requirements};
    pf_vkGetTensorMemoryRequirementsARM(vulkan.device, &tensor_requirement_info, &tensor_requirement);

    VkImageMemoryRequirementsInfo2 image_requirement_info = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        nullptr};
    image_requirement_info.image = image;

    VkMemoryDedicatedRequirements image_dedicated_requirements = {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
        nullptr};
    VkMemoryRequirements2 image_requirement = {
        VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        &image_dedicated_requirements};

    vkGetImageMemoryRequirements2(
        vulkan.device,
        &image_requirement_info,
        &image_requirement);
    if (tensor_dedicated_requirements.requiresDedicatedAllocation ||
        image_dedicated_requirements.requiresDedicatedAllocation)
    {
        vkDestroyImage(vulkan.device, image, nullptr);
        pf_vkDestroyTensorARM(vulkan.device, tensor, nullptr);
        return 77;
    }

    const VkDeviceSize base_alignment = std::max(
        image_requirement.memoryRequirements.alignment,
        tensor_requirement.memoryRequirements.alignment);
    const VkDeviceSize base_offset = align_up(1, base_alignment);

    VkDeviceSize allocation_size = base_offset + std::max(
                                                     image_requirement.memoryRequirements.size,
                                                     tensor_requirement.memoryRequirements.size);
    uint32_t memory_type_bits =
        image_requirement.memoryRequirements.memoryTypeBits &
        tensor_requirement.memoryRequirements.memoryTypeBits;
    assert(memory_type_bits != 0);

    uint32_t memory_type_index = get_device_memory_type(memory_type_bits, 0);

    VkMemoryAllocateInfo alloc_info = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr};
    alloc_info.allocationSize = allocation_size;
    alloc_info.memoryTypeIndex = memory_type_index;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &memory);
    check(result);

    VkBindImageMemoryInfo bind_image_info = {
        VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
        nullptr};
    bind_image_info.image = image;
    bind_image_info.memory = memory;
    bind_image_info.memoryOffset = base_offset;

    result = vkBindImageMemory2(vulkan.device, 1, &bind_image_info);
    check(result);

    VkBindTensorMemoryInfoARM bind_tensor_info = {
        VK_STRUCTURE_TYPE_BIND_TENSOR_MEMORY_INFO_ARM,
        nullptr};
    bind_tensor_info.tensor = tensor;
    bind_tensor_info.memory = memory;
    bind_tensor_info.memoryOffset = base_offset;

    result = pf_vkBindTensorMemoryARM(vulkan.device, 1, &bind_tensor_info);
    check(result);

    VkTensorViewCreateInfoARM tensor_view_info = {VK_STRUCTURE_TYPE_TENSOR_VIEW_CREATE_INFO_ARM, nullptr};
    tensor_view_info.flags = 0;
    tensor_view_info.tensor = tensor;
    tensor_view_info.format = tensor_format;
    VkTensorViewARM tensor_view = VK_NULL_HANDLE;
    result = pf_vkCreateTensorViewARM(vulkan.device, &tensor_view_info, nullptr, &tensor_view);
    check(result);

    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    result = vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &descriptor_set_layout);
    check(result);

    VkDescriptorPoolSize pool_size = {};
    pool_size.type = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    result = vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &descriptor_pool);
    check(result);

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo descriptor_alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr};
    descriptor_alloc_info.descriptorPool = descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = 1;
    descriptor_alloc_info.pSetLayouts = &descriptor_set_layout;
    result = vkAllocateDescriptorSets(vulkan.device, &descriptor_alloc_info, &descriptor_set);
    check(result);

    VkWriteDescriptorSetTensorARM tensor_descriptor_info = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_TENSOR_ARM, nullptr};
    tensor_descriptor_info.tensorViewCount = 1;
    tensor_descriptor_info.pTensorViews = &tensor_view;
    VkWriteDescriptorSet descriptor_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &tensor_descriptor_info};
    descriptor_write.dstSet = descriptor_set;
    descriptor_write.dstBinding = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
    vkUpdateDescriptorSets(vulkan.device, 1, &descriptor_write, 0, nullptr);

    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(float);

    VkPipelineLayoutCreateInfo pipeline_layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_range;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    result = vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &pipeline_layout);
    check(result);

    VkShaderModule shader = create_shader_module(vulkan, vulkan_tensors_2_preprocessing_spv, vulkan_tensors_2_preprocessing_spv_len);
    VkComputePipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr};
    pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr};
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = pipeline_layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
    check(result);

    const VkDeviceSize readback_size = width * height * 3 * sizeof(float);
    const VkDeviceSize readback_buffer_size = readback_size * 2;
    VkBufferCreateInfo readback_buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
    readback_buffer_info.size = readback_buffer_size;
    readback_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    readback_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer readback_buffer = VK_NULL_HANDLE;
    result = vkCreateBuffer(vulkan.device, &readback_buffer_info, nullptr, &readback_buffer);
    check(result);

    VkMemoryRequirements readback_requirements = {};
    vkGetBufferMemoryRequirements(vulkan.device, readback_buffer, &readback_requirements);

    VkMemoryAllocateInfo readback_alloc_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
    readback_alloc_info.allocationSize = readback_requirements.size;
    readback_alloc_info.memoryTypeIndex = get_device_memory_type(
        readback_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(vulkan.device, &readback_alloc_info, nullptr, &readback_memory);
    check(result);
    result = vkBindBufferMemory(vulkan.device, readback_buffer, readback_memory, 0);
    check(result);

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo command_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = vulkan.queue_family_index;
    result = vkCreateCommandPool(vulkan.device, &command_pool_info, nullptr, &command_pool);
    check(result);

    std::array<VkCommandBuffer, 3> command_buffers = {};
    VkCommandBufferAllocateInfo command_buffer_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
    command_buffer_alloc_info.commandPool = command_pool;
    command_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_alloc_info.commandBufferCount = command_buffers.size();
    result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_alloc_info, command_buffers.data());
    check(result);
    VkCommandBuffer clear_command_buffer = command_buffers[0];
    VkCommandBuffer dispatch_command_buffer = command_buffers[1];
    VkCommandBuffer readback_command_buffer = command_buffers[2];

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr};
    result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
    check(result);

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(clear_command_buffer, &begin_info);
    check(result);

    VkImageSubresourceRange image_range = {};
    image_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_range.baseMipLevel = 0;
    image_range.levelCount = 1;
    image_range.baseArrayLayer = 0;
    image_range.layerCount = 1;

    VkImageMemoryBarrier2 to_clear = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr};
    to_clear.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    to_clear.srcAccessMask = 0;
    to_clear.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_clear.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_clear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_clear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_clear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_clear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_clear.image = image;
    to_clear.subresourceRange = image_range;
    VkDependencyInfo dependency_info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr};
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &to_clear;
    vkCmdPipelineBarrier2(clear_command_buffer, &dependency_info);

    VkClearColorValue sentinel = {};
    sentinel.float32[0] = -12345.0f;
    sentinel.float32[1] = -12345.0f;
    sentinel.float32[2] = -12345.0f;
    sentinel.float32[3] = -12345.0f;
    vkCmdClearColorImage(clear_command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &sentinel, 1, &image_range);

    result = vkEndCommandBuffer(clear_command_buffer);
    check(result);
    submit_frame_boundary(vulkan.device, queue, clear_command_buffer, fence, 0, image, tensor, 0, nullptr);

    result = vkBeginCommandBuffer(dispatch_command_buffer, &begin_info);
    check(result);

    VkImageMemoryBarrier2 clear_to_copy = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr};
    clear_to_copy.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    clear_to_copy.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    clear_to_copy.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    clear_to_copy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    clear_to_copy.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    clear_to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    clear_to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clear_to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clear_to_copy.image = image;
    clear_to_copy.subresourceRange = image_range;
    dependency_info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr};
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &clear_to_copy;
    vkCmdPipelineBarrier2(dispatch_command_buffer, &dependency_info);

    VkBufferImageCopy copy_region = {};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = 0;
    copy_region.bufferImageHeight = 0;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset = {0, 0, 0};
    copy_region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(dispatch_command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buffer, 1, &copy_region);

    VkImageMemoryBarrier2 copy_to_shader = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr};
    copy_to_shader.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    copy_to_shader.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    copy_to_shader.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    copy_to_shader.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    copy_to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copy_to_shader.newLayout = VK_IMAGE_LAYOUT_TENSOR_ALIASING_ARM;
    copy_to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copy_to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copy_to_shader.image = image;
    copy_to_shader.subresourceRange = image_range;
    dependency_info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr};
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &copy_to_shader;
    vkCmdPipelineBarrier2(dispatch_command_buffer, &dependency_info);

    const float time_seconds = 0.25f;
    vkCmdBindPipeline(dispatch_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(dispatch_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
    vkCmdPushConstants(dispatch_command_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(time_seconds), &time_seconds);
    vkCmdDispatch(dispatch_command_buffer, width, height, 1);

    result = vkEndCommandBuffer(dispatch_command_buffer);
    check(result);
    submit_frame_boundary(vulkan.device, queue, dispatch_command_buffer, fence, 1, image, tensor, 0, nullptr);

    result = vkBeginCommandBuffer(readback_command_buffer, &begin_info);
    check(result);

    VkTensorMemoryBarrierARM tensor_barrier = {VK_STRUCTURE_TYPE_TENSOR_MEMORY_BARRIER_ARM, nullptr};
    tensor_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    tensor_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    tensor_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    tensor_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    tensor_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tensor_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tensor_barrier.tensor = tensor;
    VkTensorDependencyInfoARM tensor_dependency_info = {
        VK_STRUCTURE_TYPE_TENSOR_DEPENDENCY_INFO_ARM, nullptr, 1, &tensor_barrier};

    VkImageMemoryBarrier2 to_copy = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr};
    to_copy.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    to_copy.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    to_copy.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_copy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    to_copy.oldLayout = VK_IMAGE_LAYOUT_TENSOR_ALIASING_ARM;
    to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_copy.image = image;
    to_copy.subresourceRange = image_range;
    dependency_info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, &tensor_dependency_info};
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &to_copy;
    vkCmdPipelineBarrier2(readback_command_buffer, &dependency_info);

    copy_region.bufferOffset = readback_size;
    vkCmdCopyImageToBuffer(readback_command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buffer, 1, &copy_region);

    VkBufferMemoryBarrier2 readback_barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr};
    readback_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    readback_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    readback_barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    readback_barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    readback_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback_barrier.buffer = readback_buffer;
    readback_barrier.offset = 0;
    readback_barrier.size = VK_WHOLE_SIZE;
    dependency_info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr};
    dependency_info.bufferMemoryBarrierCount = 1;
    dependency_info.pBufferMemoryBarriers = &readback_barrier;
    vkCmdPipelineBarrier2(readback_command_buffer, &dependency_info);

    result = vkEndCommandBuffer(readback_command_buffer);
    check(result);
    submit_frame_boundary(vulkan.device, queue, readback_command_buffer, fence, 2, image, tensor, 1, &readback_buffer);

    if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
    {
        void *readback_data = nullptr;
        result = vkMapMemory(vulkan.device, readback_memory, 0, readback_buffer_size, 0, &readback_data);
        check(result);
        std::vector<float> expected(width * height * 3, -12345.0f);
        const size_t expected_size = expected.size() * sizeof(float);
        const char *readback_bytes = static_cast<const char *>(readback_data);
        const bool clear_wrote_sentinel = std::memcmp(readback_bytes, expected.data(), expected_size) == 0;
        const float *alias_values = reinterpret_cast<const float *>(readback_bytes + readback_size);
        size_t changed_value_count = 0;
        for (size_t i = 0; i < expected.size(); i++)
        {
            if (alias_values[i] != expected[i])
                changed_value_count++;
        }
        vkUnmapMemory(vulkan.device, readback_memory);
        assert(clear_wrote_sentinel);
        assert(changed_value_count > expected.size() / 2);
    }
    if (vulkan.vkAssertBuffer)
    {
        uint32_t readback_crc = 0;
        const VkUpdateBufferInfoARM readback_info{
            VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM,
            nullptr,
            readback_buffer,
            0,
            VK_WHOLE_SIZE,
            nullptr};
        result = vulkan.vkAssertBuffer(vulkan.device, &readback_info, &readback_crc, "tensor/image aliasing readback buffer");
        check(result);
        (void)readback_crc;
    }
    vkDestroyFence(vulkan.device, fence, nullptr);
    vkFreeCommandBuffers(vulkan.device, command_pool, command_buffers.size(), command_buffers.data());
    vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
    vkDestroyBuffer(vulkan.device, readback_buffer, nullptr);
    testFreeMemory(vulkan, readback_memory);
    vkDestroyPipeline(vulkan.device, pipeline, nullptr);
    vkDestroyShaderModule(vulkan.device, shader, nullptr);
    vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
    vkDestroyDescriptorPool(vulkan.device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(vulkan.device, descriptor_set_layout, nullptr);
    pf_vkDestroyTensorViewARM(vulkan.device, tensor_view, nullptr);
    vkDestroyImage(vulkan.device, image, nullptr);
    pf_vkDestroyTensorARM(vulkan.device, tensor, nullptr);
    testFreeMemory(vulkan, memory);

    bench_stop_iteration(vulkan.bench);

    test_done(vulkan);

    return 0;
}
