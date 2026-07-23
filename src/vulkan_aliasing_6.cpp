#include "vulkan_common.h"

static uint16_t buffer_count = 5;

int main(int argc, char **argv)
{
    vulkan_req_t reqs;

    vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_aliasing_6", reqs);
    std::vector<VkBuffer> buffers(buffer_count);
    uint32_t expected_crc = 0;
    VkResult result;
    VkQueue queue;
    vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

    VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
    bufferCreateInfo.size = 512;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    for (uint16_t i = 0; i < buffer_count; i++)
    {
        result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffers[i]);
        assert(result == VK_SUCCESS);
    }
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(vulkan.device, buffers[0], &memory_requirements);
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

    for (uint16_t i = 0; i < buffer_count; i++)
    {
        result = vkBindBufferMemory(vulkan.device, buffers[i], memory, offset);
        check(result);
    }
    char *data = nullptr;
    result = vkMapMemory(vulkan.device, memory, offset, 512, 0, (void **)&data);
    assert(result == VK_SUCCESS);
    memset(data, 0xdeaddead, 512);
    expected_crc = adler32((unsigned char *)data, 512);
    if (vulkan.has_explicit_host_updates)
        testFlushMemory(vulkan, memory, offset, 512, true);
    vkUnmapMemory(vulkan.device, memory);

    testQueueBuffer(vulkan, queue, buffers);

    if (vulkan.vkAssertBuffer)
    {
        for (uint16_t i = 0; i < buffer_count; i++)
        {
            uint32_t buffer_crc = 0;
            const VkUpdateBufferInfoARM buffer_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffers[i], 0, VK_WHOLE_SIZE, nullptr};
            char name[32];
            snprintf(name, sizeof(name), "buffer_%u", i);
            result = vulkan.vkAssertBuffer(vulkan.device, &buffer_info, &buffer_crc, name);
            check(result);
            assert(buffer_crc == expected_crc);
            (void)buffer_crc;
        }
    }
    for (uint16_t i = 0; i < buffer_count; i++)
    {
        vkDestroyBuffer(vulkan.device, buffers[i], nullptr);
    }
    testFreeMemory(vulkan, memory);
    test_done(vulkan);
    return 0;
}
