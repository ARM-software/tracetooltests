#include "vulkan_common.h"

static const uint32_t buffer_size = 1024;
static int order_count = 4;

struct order_case
{
    const char *name;
    std::array<uint32_t, 3> bind_order;
    std::array<uint32_t, 3> destroy_order;
};

static const std::array<order_case, 4> order_cases = {{
    {"bind_123_destroy_321", {0, 1, 2}, {2, 1, 0}},
    {"bind_213_destroy_312", {1, 0, 2}, {2, 0, 1}},
    {"bind_321_destroy_132", {2, 1, 0}, {0, 2, 1}},
    {"bind_231_destroy_213", {1, 2, 0}, {1, 0, 2}},
}};

static void show_usage()
{
    printf("-o/--orders N          Set number of binding order cases to run (default %d, max %zu)\n", order_count, order_cases.size());
}

static bool test_cmdopt(int &i, int argc, char **argv, vulkan_req_t &reqs)
{
    if (match(argv[i], "-o", "--orders"))
    {
        order_count = get_arg(argv, ++i, argc);
        return order_count > 0 && order_count <= static_cast<int>(order_cases.size());
    }
    return false;
}

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
    const VkDeviceSize align_mod = value % alignment;
    return (align_mod == 0) ? value : (value + alignment - align_mod);
}

static void assert_buffer(const vulkan_setup_t &vulkan, VkBuffer buffer, uint32_t expected_crc, const char *name)
{
    if (vulkan.vkAssertBuffer)
    {
        uint32_t buffer_crc = 0;
        const VkUpdateBufferInfoARM buffer_info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, buffer, 0, VK_WHOLE_SIZE, nullptr};
        VkResult result = vulkan.vkAssertBuffer(vulkan.device, &buffer_info, &buffer_crc, name);
        check(result);
        if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
            assert(buffer_crc == expected_crc);
    }
}

static void run_case(const vulkan_setup_t &vulkan, VkQueue queue, const order_case &current_case)
{
    std::array<VkBuffer, 3> buffers = {};
    std::array<uint32_t, 3> expected_crc = {};
    const std::array<int, 3> fill_patterns = {0x11, 0x22, 0x33};

    VkBufferCreateInfo buffer_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
    buffer_create_info.size = buffer_size;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    for (VkBuffer &buffer : buffers)
    {
        VkResult result = vkCreateBuffer(vulkan.device, &buffer_create_info, nullptr, &buffer);
        check(result);
        assert(buffer != VK_NULL_HANDLE);
    }

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(vulkan.device, buffers[0], &memory_requirements);
    assert(memory_requirements.alignment > 0);

    const VkMemoryPropertyFlags memory_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const uint32_t memory_type_index = get_device_memory_type(memory_requirements.memoryTypeBits, memory_flags);
    const VkDeviceSize aligned_size = align_up(memory_requirements.size, memory_requirements.alignment);

    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
    allocate_info.memoryTypeIndex = memory_type_index;
    allocate_info.allocationSize = aligned_size * buffers.size();

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &memory);
    check(result);
    assert(memory != VK_NULL_HANDLE);

    for (uint32_t bind_index = 0; bind_index < current_case.bind_order.size(); bind_index++)
    {
        const uint32_t buffer_index = current_case.bind_order[bind_index];
        result = vkBindBufferMemory(vulkan.device, buffers[buffer_index], memory, aligned_size * bind_index);
        check(result);
    }

    char *data = nullptr;
    result = vkMapMemory(vulkan.device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&data);
    check(result);

    for (uint32_t bind_index = 0; bind_index < current_case.bind_order.size(); bind_index++)
    {
        const uint32_t buffer_index = current_case.bind_order[bind_index];
        char *buffer_data = data + aligned_size * bind_index;
        memset(buffer_data, fill_patterns[buffer_index], buffer_size);
        expected_crc[buffer_index] = adler32((unsigned char *)buffer_data, buffer_size);
        if (vulkan.has_explicit_host_updates)
            testFlushMemory(vulkan, memory, aligned_size * bind_index, buffer_size, true);
    }
    vkUnmapMemory(vulkan.device, memory);

    testQueueBuffer(vulkan, queue, {buffers[0], buffers[1], buffers[2]});
    for (uint32_t buffer_index = 0; buffer_index < buffers.size(); buffer_index++)
    {
        std::string name = std::string(current_case.name) + "_buffer" + std::to_string(buffer_index + 1);
        assert_buffer(vulkan, buffers[buffer_index], expected_crc[buffer_index], name.c_str());
    }

    for (uint32_t buffer_index : current_case.destroy_order)
    {
        vkDestroyBuffer(vulkan.device, buffers[buffer_index], nullptr);
        buffers[buffer_index] = VK_NULL_HANDLE;
    }

    testFreeMemory(vulkan, memory);
}

int main(int argc, char **argv)
{
    vulkan_req_t reqs{};
    reqs.usage = show_usage;
    reqs.cmdopt = test_cmdopt;
    vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_buffer_bind_lifetime", reqs);

    VkQueue queue;
    vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

    for (int i = 0; i < order_count; i++)
    {
        run_case(vulkan, queue, order_cases[i]);
    }

    test_done(vulkan);
    return 0;
}
