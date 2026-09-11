#include "vulkan_common.h"

static constexpr uint32_t kWidth = 16;
static constexpr uint32_t kHeight = 16;

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
    assert(alignment > 0);
    return ((value + alignment - 1) / alignment) * alignment;
}

int main(int argc, char **argv)
{
    vulkan_req_t reqs{};
    reqs.minApiVersion = VK_API_VERSION_1_3;
    reqs.apiVersion = VK_API_VERSION_1_3;
    reqs.bufferDeviceAddress = true;
    reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
    reqs.reqfeat13.dynamicRendering = VK_TRUE;
    reqs.device_extensions.push_back(VK_ARM_PERFORMANCE_COUNTERS_BY_REGION_EXTENSION_NAME);

    VkPhysicalDevicePerformanceCountersByRegionFeaturesARM pc_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_COUNTERS_BY_REGION_FEATURES_ARM,
        nullptr,
        VK_TRUE};
    reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&pc_features);
    vulkan_setup_t vulkan = test_init(argc, argv, "performance_counters", reqs);

    bench_start_iteration(vulkan.bench);

    VkPhysicalDevicePerformanceCountersByRegionPropertiesARM counters_properties{};
    counters_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_COUNTERS_BY_REGION_PROPERTIES_ARM;

    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &counters_properties;
    vkGetPhysicalDeviceProperties2(vulkan.physical, &properties);
    assert(counters_properties.maxPerRegionPerformanceCounters >= 1);

    auto enumerate_counters = reinterpret_cast<PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM>(
        vkGetInstanceProcAddr(
            vulkan.instance, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM"));
    assert(enumerate_counters);

    uint32_t counter_count = 0;
    VkResult result = enumerate_counters(
        vulkan.physical, vulkan.queue_family_index, &counter_count, nullptr, nullptr);
    check(result);
    assert(counter_count > 0);
    std::vector<VkPerformanceCounterARM> counters(counter_count);
    std::vector<VkPerformanceCounterDescriptionARM> counter_descriptions(counter_count);

    for (uint32_t i = 0; i < counter_count; i++)
    {
        counters[i].sType = VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_ARM;
        counter_descriptions[i].sType =
            VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_ARM;
    }

    result = enumerate_counters(vulkan.physical, vulkan.queue_family_index, &counter_count, counters.data(), counter_descriptions.data());
    check(result);
    assert(counters_properties.maxPerRegionPerformanceCounters >= 1);
    assert(counters_properties.performanceCounterRegionSize.width >= 1);
    assert(counters_properties.performanceCounterRegionSize.height >= 1);
    assert(counters_properties.rowStrideAlignment >= 1);
    assert(counters_properties.regionAlignment >= 1);

    const uint32_t selected_counter_count = 1;
    assert(selected_counter_count <= counters_properties.maxPerRegionPerformanceCounters);
    uint32_t selected_counter = counters[0].counterID;
    const uint32_t region_count_x =
        (kWidth + counters_properties.performanceCounterRegionSize.width - 1) /
        counters_properties.performanceCounterRegionSize.width;
    const uint32_t region_count_y =
        (kHeight + counters_properties.performanceCounterRegionSize.height - 1) /
        counters_properties.performanceCounterRegionSize.height;
    const VkDeviceSize region_stride = align_up(
        selected_counter_count * sizeof(uint32_t), counters_properties.regionAlignment);
    const VkDeviceSize row_stride = align_up(
        region_count_x * region_stride, counters_properties.rowStrideAlignment);
    const VkDeviceSize counter_data_size = row_stride * region_count_y;
    const VkDeviceSize counter_buffer_size =
        counter_data_size + counters_properties.regionAlignment - 1;

    VkBufferCreateInfo counter_buffer_info{};
    counter_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    counter_buffer_info.size = counter_buffer_size;
    counter_buffer_info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    counter_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer counter_buffer = VK_NULL_HANDLE;
    result = vkCreateBuffer(vulkan.device, &counter_buffer_info, nullptr, &counter_buffer);
    check(result);
    test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)counter_buffer,
        "performance counters by region output");

    VkMemoryRequirements counter_memory_requirements{};
    vkGetBufferMemoryRequirements(vulkan.device, counter_buffer, &counter_memory_requirements);

    VkMemoryAllocateFlagsInfo counter_memory_flags{};
    counter_memory_flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    counter_memory_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo counter_memory_info{};
    counter_memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    counter_memory_info.pNext = &counter_memory_flags;
    counter_memory_info.allocationSize = counter_memory_requirements.size;
    counter_memory_info.memoryTypeIndex = get_device_memory_type(
        counter_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    VkPhysicalDeviceMemoryProperties counter_memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &counter_memory_properties);
    const bool counter_memory_coherent =
        (counter_memory_properties.memoryTypes[counter_memory_info.memoryTypeIndex].propertyFlags &
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    VkDeviceMemory counter_memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(vulkan.device, &counter_memory_info, nullptr, &counter_memory);
    check(result);
    result = vkBindBufferMemory(vulkan.device, counter_buffer, counter_memory, 0);
    check(result);

    void* mapped_counter_data = nullptr;
    result = vkMapMemory(vulkan.device, counter_memory, 0, counter_buffer_size, 0, &mapped_counter_data);
    check(result);
    memset(mapped_counter_data, 0, counter_buffer_size);
    if (!counter_memory_coherent || vulkan.has_explicit_host_updates)
    {
        testFlushMemory(vulkan, counter_memory, 0, VK_WHOLE_SIZE, counter_memory_coherent);
    }
    vkUnmapMemory(vulkan.device, counter_memory);

    VkBufferDeviceAddressInfo counter_address_info{};
    counter_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    counter_address_info.buffer = counter_buffer;
    const VkDeviceAddress counter_buffer_address =
        vulkan.vkGetBufferDeviceAddress(vulkan.device, &counter_address_info);
    assert(counter_buffer_address != 0);
    const VkDeviceAddress counter_data_address =
        align_up(counter_buffer_address, counters_properties.regionAlignment);
    const VkDeviceSize counter_data_offset = counter_data_address - counter_buffer_address;
    assert(counter_data_offset + counter_data_size <= counter_buffer_size);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = {kWidth, kHeight, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    VkImage image = VK_NULL_HANDLE;
    result = vkCreateImage(vulkan.device, &image_info, nullptr, &image);
    check(result);
    test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)image,
        "performance counters by region color target");

    VkMemoryRequirements memory_requirements{};
    vkGetImageMemoryRequirements(vulkan.device, image, &memory_requirements);

    VkMemoryAllocateInfo memory_info{};
    memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_info.allocationSize = memory_requirements.size;
    memory_info.memoryTypeIndex = get_device_memory_type(
        memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(vulkan.device, &memory_info, nullptr, &image_memory);
    check(result);

    result = vkBindImageMemory(vulkan.device, image, image_memory, 0);
    check(result);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_info.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    VkImageView image_view = VK_NULL_HANDLE;
    result = vkCreateImageView(vulkan.device, &view_info, nullptr, &image_view);
    check(result);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = vulkan.queue_family_index;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
    check(result);

    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_info, &command_buffer);
    check(result);
    test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer,
        "performance counters by region command buffer");

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(command_buffer, &begin_info);
    check(result);

    VkImageMemoryBarrier image_barrier{};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.srcAccessMask = 0;
    image_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    VkRenderPassPerformanceCountersByRegionBeginInfoARM counter_begin_info{};
    counter_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_PERFORMANCE_COUNTERS_BY_REGION_BEGIN_INFO_ARM;
    counter_begin_info.counterAddressCount = 1;
    counter_begin_info.pCounterAddresses = &counter_data_address;
    counter_begin_info.serializeRegions = VK_TRUE;
    counter_begin_info.counterIndexCount = selected_counter_count;
    counter_begin_info.pCounterIndices = &selected_counter;
    rendering_info.pNext = &counter_begin_info;
    rendering_info.renderArea = {{0, 0}, {kWidth, kHeight}};
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;

    vkCmdBeginRendering(command_buffer, &rendering_info);
    vkCmdEndRendering(command_buffer);

    result = vkEndCommandBuffer(command_buffer);
    check(result);

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    check(result);
    result = vkQueueWaitIdle(queue);
    check(result);

    test_marker_mention(vulkan, "Captured VK_ARM_performance_counters_by_region data",
                        VK_OBJECT_TYPE_BUFFER, (uint64_t)counter_buffer);
    if (vulkan.vkAssertBuffer)
    {
        if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
        {
            const VkUpdateBufferInfoARM assert_info{
                VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, counter_buffer,
                counter_data_offset, counter_data_size, nullptr};
            uint32_t counter_crc = 0;
            result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &counter_crc,
                                           "performance counters by region output");
            check(result);
            (void)counter_crc;
        }
    }

    vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
    vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
    vkDestroyImageView(vulkan.device, image_view, nullptr);
    vkDestroyImage(vulkan.device, image, nullptr);
    vkFreeMemory(vulkan.device, image_memory, nullptr);
    vkDestroyBuffer(vulkan.device, counter_buffer, nullptr);
    vkFreeMemory(vulkan.device, counter_memory, nullptr);

    bench_stop_iteration(vulkan.bench);

    test_done(vulkan);

    return 0;
}
