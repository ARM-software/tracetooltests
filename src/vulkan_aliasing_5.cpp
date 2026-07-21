#include "vulkan_common.h"

int main(int argc, char **argv)
{
    vulkan_req_t reqs;

    vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_aliasing_5", reqs);
    VkBuffer buffer1;
    VkBuffer buffer2;
    uint32_t expected_crc = 0;
    VkQueue queue;
    vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

    VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
    bufferCreateInfo.size = 512;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer1);
    assert(result == VK_SUCCESS);
    result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer2);
    assert(result == VK_SUCCESS);

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(vulkan.device, buffer1, &memory_requirements);
    VkMemoryPropertyFlags memoryflags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    const VkDeviceSize offset = memory_requirements.alignment;
    assert(offset > 0);
    const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, memoryflags);
    VkDeviceSize aligned_size = offset + memory_requirements.size;

    VkMemoryAllocateInfo pAllocateMemInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
    pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
    pAllocateMemInfo.allocationSize = aligned_size;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
    check(result);
    assert(memory != VK_NULL_HANDLE);
    result = vkBindBufferMemory(vulkan.device, buffer1, memory, offset);
    check(result);
    result = vkBindBufferMemory(vulkan.device, buffer2, memory, offset);
    check(result);
    char *data = nullptr;
    result = vkMapMemory(vulkan.device, memory, offset, 512, 0, (void **)&data);
    assert(result == VK_SUCCESS);
    memset(data, 0xdeaddead, 512);
    expected_crc = adler32((unsigned char *)data, 512);
    if (vulkan.has_explicit_host_updates)
        testFlushMemory(vulkan, memory, offset, 512, true);
    vkUnmapMemory(vulkan.device, memory);

    testQueueBuffer(vulkan, queue, {buffer1, buffer2});

    if (vulkan.vkAssertBuffer)
    {
        uint32_t buffer1_crc = 0;
        const VkUpdateBufferInfoARM buffer1_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffer1, 0, VK_WHOLE_SIZE, nullptr};
        result = vulkan.vkAssertBuffer(vulkan.device, &buffer1_info, &buffer1_crc, "buffer1");
        check(result);
        assert(buffer1_crc == expected_crc);
        (void)buffer1_crc;
        uint32_t buffer2_crc = 0;
        const VkUpdateBufferInfoARM buffer2_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffer2, 0, VK_WHOLE_SIZE, nullptr};
        result = vulkan.vkAssertBuffer(vulkan.device, &buffer2_info, &buffer2_crc, "buffer2");
        check(result);
        assert(buffer2_crc == expected_crc);
        (void)buffer2_crc;
    }

    vkDestroyBuffer(vulkan.device, buffer1, nullptr);
    vkDestroyBuffer(vulkan.device, buffer2, nullptr);
    testFreeMemory(vulkan, memory);
    test_done(vulkan);
    return 0;
}
