#include "vulkan_common.h"

static bool find_memory_type(
    VkPhysicalDevice physical,
    uint32_t type_filter,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags forbidden,
    uint32_t *memory_type_index)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);

    for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
    {
        const VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if ((type_filter & (1u << i)) != 0 &&
            (flags & required) == required &&
            (flags & forbidden) == 0)
        {
            *memory_type_index = i;
            return true;
        }
    }

    return false;
}

int main(int argc, char **argv)
{
    vulkan_req_t reqs{};
    reqs.apiVersion = VK_API_VERSION_1_2;
    reqs.minApiVersion = VK_API_VERSION_1_2;
    reqs.bufferDeviceAddress = true;
    reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
    vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_dedicated_memory", reqs);
    VkQueue queue;
    vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkBuffer readback_buffer = VK_NULL_HANDLE;
    VkBuffer dedicated_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    VkDeviceMemory dedicated_memory = VK_NULL_HANDLE;

    const VkDeviceSize buffer_size = 512;
    const VkDeviceSize address_offset = 0;
    assert(buffer_size >= sizeof(VkDeviceAddress));
    uint32_t expected_crc = 0;

    VkBufferCreateInfo staging_buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
    staging_buffer_info.size = buffer_size;
    staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(vulkan.device, &staging_buffer_info, nullptr, &staging_buffer);
    check(result);

    VkBufferCreateInfo dedicated_buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
    dedicated_buffer_info.size = buffer_size;
    dedicated_buffer_info.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    dedicated_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = vkCreateBuffer(vulkan.device, &dedicated_buffer_info, nullptr, &dedicated_buffer);
    check(result);

    VkBufferCreateInfo readback_buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
    readback_buffer_info.size = buffer_size;
    readback_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    readback_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = vkCreateBuffer(vulkan.device, &readback_buffer_info, nullptr, &readback_buffer);
    check(result);

    VkMemoryRequirements readback_requirements;
    vkGetBufferMemoryRequirements(vulkan.device, readback_buffer, &readback_requirements);
    VkMemoryRequirements staging_requirements;
    vkGetBufferMemoryRequirements(vulkan.device, staging_buffer, &staging_requirements);
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(vulkan.device, dedicated_buffer, &memory_requirements);

    VkMemoryAllocateInfo staging_allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
    staging_allocate_info.memoryTypeIndex = get_device_memory_type(
        staging_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging_allocate_info.allocationSize = staging_requirements.size;

    VkMemoryDedicatedAllocateInfo dedicated_info = {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        nullptr};
    dedicated_info.buffer = dedicated_buffer;
    dedicated_info.image = VK_NULL_HANDLE;

    VkMemoryAllocateFlagsInfo dedicated_flags_info = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        &dedicated_info};
    dedicated_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    dedicated_flags_info.deviceMask = 0;

    VkMemoryAllocateInfo readback_allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
    readback_allocate_info.memoryTypeIndex = get_device_memory_type(
        readback_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    readback_allocate_info.allocationSize = readback_requirements.size;

    result = vkAllocateMemory(vulkan.device, &staging_allocate_info, nullptr, &staging_memory);
    check(result);
    assert(staging_memory != VK_NULL_HANDLE);

    result = vkBindBufferMemory(vulkan.device, staging_buffer, staging_memory, 0);
    check(result);

    uint32_t dedicated_memory_type = 0;
    if (!find_memory_type(
            vulkan.physical,
            memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            &dedicated_memory_type))
    {
        printf("Skipping: no device-local-only memory type available for dedicated buffer\n");
        vkDestroyBuffer(vulkan.device, readback_buffer, nullptr);
        vkDestroyBuffer(vulkan.device, dedicated_buffer, nullptr);
        vkDestroyBuffer(vulkan.device, staging_buffer, nullptr);
        testFreeMemory(vulkan, staging_memory);
        test_done(vulkan);
        return 77;
    }
    VkMemoryAllocateInfo dedicated_allocate_info = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        &dedicated_flags_info};
    dedicated_allocate_info.memoryTypeIndex = dedicated_memory_type;
    dedicated_allocate_info.allocationSize = memory_requirements.size;

    result = vkAllocateMemory(vulkan.device, &dedicated_allocate_info, nullptr, &dedicated_memory);
    check(result);
    assert(dedicated_memory != VK_NULL_HANDLE);

    result = vkBindBufferMemory(vulkan.device, dedicated_buffer, dedicated_memory, 0);
    check(result);

    VkBufferDeviceAddressInfo bdainfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr};
    bdainfo.buffer = dedicated_buffer;
    VkDeviceAddress address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &bdainfo);
    assert(address != 0);

    char *data = nullptr;
    result = vkMapMemory(vulkan.device, staging_memory, 0, buffer_size, 0, (void **)&data);
    check(result);

    memset(data, 0xde, buffer_size);
    memcpy(data + address_offset, &address, sizeof(address));
    expected_crc = adler32((unsigned char *)data, buffer_size);
    testFlushMemoryDeviceAddresses(vulkan, staging_memory, 0, buffer_size, {address_offset}, VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM, true);
    vkUnmapMemory(vulkan.device, staging_memory);

    result = vkAllocateMemory(vulkan.device, &readback_allocate_info, nullptr, &readback_memory);
    check(result);
    assert(readback_memory != VK_NULL_HANDLE);

    result = vkBindBufferMemory(vulkan.device, readback_buffer, readback_memory, 0);
    check(result);

    const VkMarkingTypeARM marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
    VkMarkingSubTypeARM marking_sub_type = {};
    marking_sub_type.deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
    VkMarkedOffsetsARM buffer_marking = {VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr};
    buffer_marking.count = 1;
    buffer_marking.pMarkingTypes = &marking_type;
    buffer_marking.pSubTypes = &marking_sub_type;
    buffer_marking.pOffsets = &address_offset;

    bench_start_iteration(vulkan.bench);
    testCopyBuffer(vulkan, queue, dedicated_buffer, staging_buffer, buffer_size);
    testQueueBuffer(vulkan, queue, {dedicated_buffer});

    if (vulkan.vkAssertBuffer)
    {
        uint32_t buffer_crc = 0;
        const VkUpdateBufferInfoARM buffer_info{
            VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM,
            &buffer_marking,
            dedicated_buffer,
            0,
            buffer_size,
            nullptr};

        result = vulkan.vkAssertBuffer(vulkan.device, &buffer_info, &buffer_crc, "dedicated device-local buffer");
        assert(result == VK_SUCCESS || result == VK_INCOMPLETE);

        if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
        {
            assert(buffer_crc == expected_crc);
        }
    }

    testCopyBuffer(vulkan, queue, readback_buffer, dedicated_buffer, buffer_size);
    testQueueBuffer(vulkan, queue, {readback_buffer});

    if (vulkan.vkAssertBuffer)
    {
        uint32_t readback_crc = 0;
        const VkUpdateBufferInfoARM readback_info{
            VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM,
            &buffer_marking,
            readback_buffer,
            0,
            buffer_size,
            nullptr};

        result = vulkan.vkAssertBuffer(vulkan.device, &readback_info, &readback_crc, "dedicated memory readback buffer");
        check(result);

        if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
        {
            assert(readback_crc == expected_crc);
        }
    }
    bench_stop_iteration(vulkan.bench);

    void *readback_data = nullptr;
    result = vkMapMemory(vulkan.device, readback_memory, 0, buffer_size, 0, &readback_data);
    check(result);
    if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
    {
        assert(adler32((unsigned char *)readback_data, buffer_size) == expected_crc);
    }
    vkUnmapMemory(vulkan.device, readback_memory);

    vkDestroyBuffer(vulkan.device, readback_buffer, nullptr);
    testFreeMemory(vulkan, readback_memory);
    vkDestroyBuffer(vulkan.device, dedicated_buffer, nullptr);
    testFreeMemory(vulkan, dedicated_memory);
    vkDestroyBuffer(vulkan.device, staging_buffer, nullptr);
    testFreeMemory(vulkan, staging_memory);
    test_done(vulkan);
    return 0;
}
