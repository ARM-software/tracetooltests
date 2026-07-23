#include "vulkan_common.h"

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
    assert(alignment > 0);
    const VkDeviceSize remainder = value % alignment;
    if (remainder == 0)
        return value;
    return value + alignment - remainder;
}

static VkDeviceSize gcd(VkDeviceSize a, VkDeviceSize b)
{
    while (b != 0)
    {
        const VkDeviceSize r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static VkDeviceSize common_alignment(VkDeviceSize a, VkDeviceSize b)
{
    assert(a > 0);
    assert(b > 0);
    return (a / gcd(a, b)) * b;
}

int main(int argc, char **argv)
{
    vulkan_req_t reqs;
    vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_aliasing_multilevel_containment", reqs);
    VkBuffer buffer_big;
    VkBuffer buffer_medium;
    VkBuffer buffer_small;

    VkQueue queue;
    uint32_t orig_crc_big = 0;
    uint32_t orig_crc_medium = 0;
    uint32_t orig_crc_small = 0;

    vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

    VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const VkDeviceSize small_size = 256;
    const VkDeviceSize intended_containment_offset = 256;

    bufferCreateInfo.size = small_size;
    VkResult result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer_small);
    assert(result == VK_SUCCESS);

    VkMemoryRequirements small_reqs;
    vkGetBufferMemoryRequirements(vulkan.device, buffer_small, &small_reqs);

    const VkDeviceSize small_relative_offset = align_up(intended_containment_offset, small_reqs.alignment);
    const VkDeviceSize medium_size = small_relative_offset + small_size;

    bufferCreateInfo.size = medium_size;
    result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer_medium);
    assert(result == VK_SUCCESS);

    VkMemoryRequirements medium_reqs;
    vkGetBufferMemoryRequirements(vulkan.device, buffer_medium, &medium_reqs);

    const VkDeviceSize medium_offset = align_up(intended_containment_offset, common_alignment(medium_reqs.alignment, small_reqs.alignment));
    const VkDeviceSize small_offset = medium_offset + small_relative_offset;
    const VkDeviceSize big_size = medium_offset + medium_size;

    bufferCreateInfo.size = big_size;
    result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer_big);
    assert(result == VK_SUCCESS);

    VkMemoryRequirements big_reqs;
    vkGetBufferMemoryRequirements(vulkan.device, buffer_big, &big_reqs);

    assert(medium_offset % medium_reqs.alignment == 0);
    assert(small_offset % small_reqs.alignment == 0);
    assert(medium_offset + medium_size <= big_size);
    assert(small_offset + small_size <= medium_offset + medium_size);

    VkMemoryPropertyFlags memoryflags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    const uint32_t memory_type_bits =
        big_reqs.memoryTypeBits & medium_reqs.memoryTypeBits & small_reqs.memoryTypeBits;
    assert(memory_type_bits != 0);

    const uint32_t memoryTypeIndex = get_device_memory_type(memory_type_bits, memoryflags);

    VkDeviceSize allocation_size = big_reqs.size;
    if (allocation_size < medium_offset + medium_reqs.size)
        allocation_size = medium_offset + medium_reqs.size;
    if (allocation_size < small_offset + small_reqs.size)
        allocation_size = small_offset + small_reqs.size;
    assert(allocation_size >= big_size);

    VkMemoryAllocateInfo pAllocateMemInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
    pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
    pAllocateMemInfo.allocationSize = allocation_size;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
    check(result);
    assert(memory != VK_NULL_HANDLE);

    result = vkBindBufferMemory(vulkan.device, buffer_big, memory, 0);
    check(result);
    result = vkBindBufferMemory(vulkan.device, buffer_medium, memory, medium_offset);
    check(result);
    result = vkBindBufferMemory(vulkan.device, buffer_small, memory, small_offset);
    check(result);

    char *data = nullptr;

    result = vkMapMemory(vulkan.device, memory, 0, big_size, 0, (void **)&data);
    assert(result == VK_SUCCESS);
    for (VkDeviceSize i = 0; i < big_size; i++)
    {
        data[i] = static_cast<char>((i * 13) & 0xff);
    }
    orig_crc_big = adler32((unsigned char *)data, big_size);
    orig_crc_medium = adler32((unsigned char *)data + medium_offset, medium_size);
    orig_crc_small = adler32((unsigned char *)data + small_offset, small_size);
    if (vulkan.has_explicit_host_updates)
        testFlushMemory(vulkan, memory, 0, big_size, true);
    vkUnmapMemory(vulkan.device, memory);
    testQueueBuffer(vulkan, queue, {buffer_big, buffer_medium, buffer_small});

    if (vulkan.vkAssertBuffer)
    {
        uint32_t big_crc = 0;
        uint32_t medium_crc = 0;
        uint32_t small_crc = 0;
        result = VK_ERROR_UNKNOWN;
        const VkUpdateBufferInfoARM parent_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffer_big, 0, VK_WHOLE_SIZE, nullptr};
        result = vulkan.vkAssertBuffer(vulkan.device, &parent_info, &big_crc, "biggest buffer");
        assert(result == VK_SUCCESS);
        assert(big_crc == orig_crc_big);
        (void)orig_crc_big;
        (void)result;
        const VkUpdateBufferInfoARM child_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffer_medium, 0, VK_WHOLE_SIZE, nullptr};
        result = vulkan.vkAssertBuffer(vulkan.device, &child_info, &medium_crc, "Medium buffer");
        assert(result == VK_SUCCESS);
        assert(medium_crc == orig_crc_medium);
        (void)orig_crc_medium;
        (void)result;
        const VkUpdateBufferInfoARM alien_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffer_small, 0, VK_WHOLE_SIZE, nullptr};
        result = vulkan.vkAssertBuffer(vulkan.device, &alien_info, &small_crc, "Small buffer");
        assert(result == VK_SUCCESS);
        assert(small_crc == orig_crc_small);
        (void)orig_crc_small;
        (void)result;
    }
    vkDestroyBuffer(vulkan.device, buffer_big, nullptr);
    vkDestroyBuffer(vulkan.device, buffer_medium, nullptr);
    vkDestroyBuffer(vulkan.device, buffer_small, nullptr);
    testFreeMemory(vulkan, memory);
    test_done(vulkan);

    return 0;
}
