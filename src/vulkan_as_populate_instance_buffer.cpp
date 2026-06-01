#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

#include <cstdint>
#include <cstring>
#include <memory>

// glslangValidator -V vulkan_as_populate_instance_buffer_copy_address.comp -o vulkan_as_populate_instance_buffer_copy_address.spirv --target-env vulkan1.2
// xxd -i vulkan_as_populate_instance_buffer_copy_address.spirv > vulkan_as_populate_instance_buffer_copy_address.inc
#include "vulkan_as_populate_instance_buffer_copy_address.inc"

// glslangValidator -V vulkan_as_populate_instance_buffer_process_instance.comp -o vulkan_as_populate_instance_buffer_process_instance.spirv --target-env vulkan1.2
// xxd -i vulkan_as_populate_instance_buffer_process_instance.spirv > vulkan_as_populate_instance_buffer_process_instance.inc
#include "vulkan_as_populate_instance_buffer_process_instance.inc"

using AsBuffer = acceleration_structures::Buffer;
using AccelerationStructure = acceleration_structures::AccelerationStructure;
using BackedAccelerationStructure = acceleration_structures::BackedAccelerationStructure;
using namespace tracetooltests;

static uint32_t bl_as_create_count = 8;
static constexpr uint32_t local_size_x = 64;

struct Vertex
{
    float pos[3];
};

struct TransformRow
{
    float values[4];
};
static_assert(sizeof(TransformRow) == 4 * sizeof(float));

// copy_address.comp copies uvec4 elements, so each 16-byte slot packs two 64-bit AS addresses.
struct PackedAddressPair
{
    uint32_t words[4];
};

static uint32_t get_copy_address_element_count()
{
    return (bl_as_create_count + 1) / 2;
}

static void write_packed_address(PackedAddressPair& packed, uint32_t slot, VkDeviceAddress address)
{
    assert(slot < 2);
    std::memcpy(&packed.words[slot * 2], &address, sizeof(address));
}

static VkDeviceAddress read_packed_address(const PackedAddressPair& packed, uint32_t slot)
{
    VkDeviceAddress address = 0;
    assert(slot < 2);
    std::memcpy(&address, &packed.words[slot * 2], sizeof(address));
    return address;
}

class AsPopulateInstanceBufferContext : public GraphicContext
{
public:
    AsPopulateInstanceBufferContext() : GraphicContext() {}
    ~AsPopulateInstanceBufferContext() {
        destroy();
    }

    void destroy() override
    {
        copy_address_pipeline = nullptr;
        copy_address_pipeline_layout = nullptr;
        copy_address_descriptor_set = nullptr;
        copy_address_descriptor_set_layout = nullptr;
        process_instance_pipeline = nullptr;
        process_instance_pipeline_layout = nullptr;
        process_instance_descriptor_set = nullptr;
        process_instance_descriptor_set_layout = nullptr;

        copy_address_source_blas_address_words_buffer = nullptr;
        blas_address_words_buffer = nullptr;
        instance_buffer = nullptr;
        process_instance_transforms_buffer = nullptr;

        if (m_vulkanSetup.device != VK_NULL_HANDLE)
        {
            acceleration_structures::destroy_backed_acceleration_structure(m_vulkanSetup, functions, backed_tl_acc_structure);
            for (BackedAccelerationStructure& blas : backed_bl_acc_structures)
            {
                acceleration_structures::destroy_backed_acceleration_structure(m_vulkanSetup, functions, blas);
            }
        }

        backed_bl_acc_structures.clear();
    }

    acceleration_structures::functions functions;
    std::vector<BackedAccelerationStructure> backed_bl_acc_structures;
    BackedAccelerationStructure backed_tl_acc_structure;

    std::unique_ptr<Buffer> copy_address_source_blas_address_words_buffer;
    std::unique_ptr<Buffer> blas_address_words_buffer;

    std::unique_ptr<Buffer> instance_buffer;
    std::unique_ptr<Buffer> process_instance_transforms_buffer;

    std::unique_ptr<DescriptorSetLayout> copy_address_descriptor_set_layout;
    std::unique_ptr<DescriptorSet> copy_address_descriptor_set;
    std::unique_ptr<PipelineLayout> copy_address_pipeline_layout;
    std::unique_ptr<ComputePipeline> copy_address_pipeline;

    std::unique_ptr<DescriptorSetLayout> process_instance_descriptor_set_layout;
    std::unique_ptr<DescriptorSet> process_instance_descriptor_set;
    std::unique_ptr<PipelineLayout> process_instance_pipeline_layout;
    std::unique_ptr<ComputePipeline> process_instance_pipeline;
};

static std::unique_ptr<AsPopulateInstanceBufferContext> p_test = nullptr;

static void show_usage()
{
    printf("Validate building a top level acceleration structure from shader-populated device addresses of N built bottom level acceleration structures.\n");
    printf("-cb/--count-bottom N   Create N bottom level acceleration structures, default is %u\n", bl_as_create_count);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
    if (match(argv[i], "-cb", "--count-bottom"))
    {
        bl_as_create_count = get_arg(argv, ++i, argc);
        return true;
    }

    (void)argc;
    (void)argv;
    (void)reqs;
    return false;
}

static void print_acceleration_structure_features(const vulkan_setup_t& vulkan)
{
    VkPhysicalDeviceAccelerationStructureFeaturesKHR features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, nullptr};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features};
    vkGetPhysicalDeviceFeatures2(vulkan.physical, &features2);

    printf(
        "Queried VkPhysicalDeviceAccelerationStructureFeaturesKHR: accelerationStructure=%u, captureReplay=%u, indirectBuild=%u, hostCommands=%u, updateAfterBind=%u\n",
        features.accelerationStructure,
        features.accelerationStructureCaptureReplay,
        features.accelerationStructureIndirectBuild,
        features.accelerationStructureHostCommands,
        features.descriptorBindingAccelerationStructureUpdateAfterBind
    );
}

static void prepare_copy_address_descriptor_resources(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    context.copy_address_descriptor_set_layout = std::make_unique<DescriptorSetLayout>(vulkan.device);
    context.copy_address_descriptor_set_layout->insertBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    context.copy_address_descriptor_set_layout->insertBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    check(context.copy_address_descriptor_set_layout->create());

    auto descriptor_pool = std::make_unique<DescriptorSetPool>(vulkan.device);
    check(descriptor_pool->create(1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}}));

    context.copy_address_descriptor_set = std::make_unique<DescriptorSet>(std::move(descriptor_pool));
    check(context.copy_address_descriptor_set->create(*context.copy_address_descriptor_set_layout));
    context.copy_address_descriptor_set->setBuffer(0, 0, *context.copy_address_source_blas_address_words_buffer);
    context.copy_address_descriptor_set->setBuffer(1, 0, *context.blas_address_words_buffer);
    context.copy_address_descriptor_set->update();
}

static void prepare_copy_address_pipeline(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    std::vector<VkDescriptorSetLayout> set_layouts = { context.copy_address_descriptor_set_layout->getHandle() };
    std::vector<VkPushConstantRange> push_constant_ranges = {
        {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)}
    };

    context.copy_address_pipeline_layout = std::make_unique<PipelineLayout>(vulkan.device);
    check(context.copy_address_pipeline_layout->create(set_layouts, push_constant_ranges));

    auto shader = std::make_unique<Shader>(vulkan.device);
    check(shader->create(
        vulkan_as_populate_instance_buffer_copy_address_spirv,
        vulkan_as_populate_instance_buffer_copy_address_spirv_len
    ));

    ShaderPipelineState shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, std::move(shader));
    context.copy_address_pipeline = std::make_unique<ComputePipeline>(vulkan.device);
    check(context.copy_address_pipeline->create(context.copy_address_pipeline_layout->getHandle(), shader_stage));
}

static void prepare_process_instance_descriptor_resources(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    context.process_instance_descriptor_set_layout = std::make_unique<DescriptorSetLayout>(vulkan.device);
    context.process_instance_descriptor_set_layout->insertBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    context.process_instance_descriptor_set_layout->insertBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    context.process_instance_descriptor_set_layout->insertBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    check(context.process_instance_descriptor_set_layout->create());

    auto descriptor_pool = std::make_unique<DescriptorSetPool>(vulkan.device);
    check(descriptor_pool->create(1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}}));

    context.process_instance_descriptor_set = std::make_unique<DescriptorSet>(std::move(descriptor_pool));
    check(context.process_instance_descriptor_set->create(*context.process_instance_descriptor_set_layout));
    context.process_instance_descriptor_set->setBuffer(0, 0, *context.instance_buffer);
    context.process_instance_descriptor_set->setBuffer(1, 0, *context.blas_address_words_buffer);
    context.process_instance_descriptor_set->setBuffer(2, 0, *context.process_instance_transforms_buffer);
    context.process_instance_descriptor_set->update();
}

static void prepare_process_instance_pipeline(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    std::vector<VkDescriptorSetLayout> set_layouts = { context.process_instance_descriptor_set_layout->getHandle() };
    std::vector<VkPushConstantRange> push_constant_ranges = {
        {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)}
    };

    context.process_instance_pipeline_layout = std::make_unique<PipelineLayout>(vulkan.device);
    check(context.process_instance_pipeline_layout->create(set_layouts, push_constant_ranges));

    auto shader = std::make_unique<Shader>(vulkan.device);
    check(shader->create(
        vulkan_as_populate_instance_buffer_process_instance_spirv,
        vulkan_as_populate_instance_buffer_process_instance_spirv_len
    ));

    ShaderPipelineState shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, std::move(shader));
    context.process_instance_pipeline = std::make_unique<ComputePipeline>(vulkan.device);
    check(context.process_instance_pipeline->create(context.process_instance_pipeline_layout->getHandle(), shader_stage));
}

static void prepare_test_resources(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    assert(bl_as_create_count > 0);
    context.functions = acceleration_structures::query_acceleration_structure_functions(vulkan);
    context.backed_bl_acc_structures = std::vector<BackedAccelerationStructure>(bl_as_create_count);

    const VkDeviceSize copy_buffer_size = sizeof(PackedAddressPair) * get_copy_address_element_count();

    context.copy_address_source_blas_address_words_buffer = std::make_unique<Buffer>(vulkan);
    check(context.copy_address_source_blas_address_words_buffer->create(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        copy_buffer_size,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ));

    context.blas_address_words_buffer = std::make_unique<Buffer>(vulkan);
    check(context.blas_address_words_buffer->create(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        copy_buffer_size,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ));

    context.instance_buffer = std::make_unique<Buffer>(vulkan);
    check(context.instance_buffer->create(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        sizeof(VkAccelerationStructureInstanceKHR) * bl_as_create_count,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ));

    context.process_instance_transforms_buffer = std::make_unique<Buffer>(vulkan);
    check(context.process_instance_transforms_buffer->create(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        sizeof(TransformRow) * bl_as_create_count * 3,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ));

    prepare_copy_address_descriptor_resources(vulkan, context);
    prepare_copy_address_pipeline(vulkan, context);
    prepare_process_instance_descriptor_resources(vulkan, context);
    prepare_process_instance_pipeline(vulkan, context);
}

static void create_build_bottom_level_acceleration_structures(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    constexpr uint32_t primitive_count = 1;
    constexpr uint32_t max_vertex = 2;
    constexpr VkDeviceSize vertex_stride = sizeof(Vertex);
    constexpr Vertex triangle_vertices[] = {
        {{0.0f, -0.5f, 0.0f}},
        {{0.5f, 0.5f, 0.0f}},
        {{-0.5f, 0.5f, 0.0f}},
    };

    assert(context.functions.vkCreateAccelerationStructureKHR);
    assert(context.functions.vkGetAccelerationStructureBuildSizesKHR);
    assert(context.functions.vkGetAccelerationStructureDeviceAddressKHR);
    assert(context.functions.vkCmdBuildAccelerationStructuresKHR);

    // one shared triangle input buffer to keep the test focused on AS creation/build.
    AsBuffer vertex_buffer = acceleration_structures::prepare_buffer(
        vulkan,
        sizeof(triangle_vertices),
        triangle_vertices,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    std::vector<VkAccelerationStructureGeometryTrianglesDataKHR> triangles_data(bl_as_create_count);
    std::vector<VkAccelerationStructureGeometryKHR> geometries(bl_as_create_count);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> build_infos(bl_as_create_count);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> range_infos(bl_as_create_count);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> range_info_ptrs(bl_as_create_count);
    std::vector<AsBuffer> scratch_buffers(bl_as_create_count);

    // First create every BLAS object and prepare one non-overlapping scratch buffer per build.
    for (uint32_t as_index = 0; as_index < bl_as_create_count; ++as_index)
    {
        VkAccelerationStructureBuildSizesInfoKHR build_size_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};

        triangles_data[as_index] = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR, nullptr};
        triangles_data[as_index].vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles_data[as_index].vertexData = vertex_buffer.address;
        triangles_data[as_index].vertexStride = vertex_stride;
        triangles_data[as_index].maxVertex = max_vertex;
        triangles_data[as_index].indexType = VK_INDEX_TYPE_NONE_KHR;
        triangles_data[as_index].indexData = {};
        triangles_data[as_index].transformData = {};

        geometries[as_index] = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
        geometries[as_index].flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometries[as_index].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometries[as_index].geometry.triangles = triangles_data[as_index];

        build_infos[as_index] = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
        build_infos[as_index].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_infos[as_index].flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build_infos[as_index].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_infos[as_index].geometryCount = 1;
        build_infos[as_index].pGeometries = &geometries[as_index];
        build_infos[as_index].srcAccelerationStructure = VK_NULL_HANDLE;

        context.functions.vkGetAccelerationStructureBuildSizesKHR(
            vulkan.device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_infos[as_index],
            &primitive_count,
            &build_size_info
        );
        assert(build_size_info.accelerationStructureSize > 0);
        assert(build_size_info.buildScratchSize > 0);

        const BackedAccelerationStructure created_blas =
            acceleration_structures::create_acceleration_structure(
                vulkan,
                context.functions,
                VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                build_size_info.accelerationStructureSize
            );
        context.backed_bl_acc_structures[as_index] = created_blas;

        scratch_buffers[as_index] = acceleration_structures::prepare_buffer(
            vulkan,
            build_size_info.buildScratchSize,
            nullptr,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        build_infos[as_index].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    	build_infos[as_index].dstAccelerationStructure = context.backed_bl_acc_structures[as_index].as.handle;
	    build_infos[as_index].scratchData.deviceAddress = scratch_buffers[as_index].address.deviceAddress;

        range_infos[as_index] = {};
        range_infos[as_index].primitiveCount = primitive_count;
        range_infos[as_index].primitiveOffset = 0;
        range_infos[as_index].firstVertex = 0;
        range_infos[as_index].transformOffset = 0;
        range_info_ptrs[as_index] = &range_infos[as_index];
    }

    // Record one batched device build for all BLAS now that all create/build inputs are ready.
    VkCommandBuffer command_buffer = context.m_defaultCommandBuffer->getHandle();
    check(vkResetCommandBuffer(command_buffer, 0));
    check(context.m_defaultCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
    context.functions.vkCmdBuildAccelerationStructuresKHR(
        command_buffer,
        static_cast<uint32_t>(build_infos.size()),
        build_infos.data(),
        range_info_ptrs.data()
    );
    check(context.m_defaultCommandBuffer->end());
    context.submit(context.m_defaultQueue, {context.m_defaultCommandBuffer}, VK_NULL_HANDLE, {}, {}, false);
    check(vkQueueWaitIdle(context.m_defaultQueue));

    // Query the final device addresses only after the device build has completed.
    for (uint32_t as_index = 0; as_index < bl_as_create_count; ++as_index)
    {
        context.backed_bl_acc_structures[as_index].as.address.deviceAddress =
            acceleration_structures::get_acceleration_structure_device_address(
                vulkan,
                context.functions,
                context.backed_bl_acc_structures[as_index].as.handle
            );
        assert(context.backed_bl_acc_structures[as_index].as.address.deviceAddress != 0);
    }

    // Scratch and shared triangle input are temporary build resources and can be released now.
    for (AsBuffer& scratch : scratch_buffers)
    {
        acceleration_structures::destroy_buffer(vulkan, scratch);
    }
    acceleration_structures::destroy_buffer(vulkan, vertex_buffer);
}

static void populate_copy_address_shader_inputs(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    const VkDeviceSize buffer_size = context.copy_address_source_blas_address_words_buffer->getSize();
    check(context.copy_address_source_blas_address_words_buffer->map(0, buffer_size));

    auto* mapped_addresses = static_cast<PackedAddressPair*>(context.copy_address_source_blas_address_words_buffer->m_mappedAddress);
    for (uint32_t packed_index = 0; packed_index < get_copy_address_element_count(); ++packed_index)
    {
        std::memset(&mapped_addresses[packed_index], 0, sizeof(PackedAddressPair));
    }

    std::vector<VkDeviceSize> marked_offsets(bl_as_create_count);
    for (uint32_t as_index = 0; as_index < bl_as_create_count; ++as_index)
    {
        const uint32_t packed_index = as_index / 2;
        const uint32_t packed_slot = as_index % 2;
        write_packed_address(mapped_addresses[packed_index], packed_slot, context.backed_bl_acc_structures[as_index].as.address.deviceAddress);
        marked_offsets[as_index] = packed_index * sizeof(PackedAddressPair) + packed_slot * sizeof(VkDeviceAddress);
    }

    if (vulkan.has_trace_helpers)
    {
        testFlushMemoryDeviceAddresses(
            vulkan,
            context.copy_address_source_blas_address_words_buffer->getMemory(),
            0,
            buffer_size,
            marked_offsets,
            VK_DEVICE_ADDRESS_TYPE_ACCELERATION_STRUCTURE_ARM,
            true
        );
    }
    else if (vulkan.has_explicit_host_updates)
    {
        context.copy_address_source_blas_address_words_buffer->flush(true);
    }

    context.copy_address_source_blas_address_words_buffer->unmap();
}

static void dispatch_copy_address_pipeline(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    const uint32_t element_count = get_copy_address_element_count();
    const uint32_t group_count_x = (element_count + local_size_x - 1) / local_size_x;
    assert(group_count_x > 0);

    VkCommandBuffer command_buffer = context.m_defaultCommandBuffer->getHandle();
    check(vkResetCommandBuffer(command_buffer, 0));
    check(context.m_defaultCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.copy_address_pipeline->getHandle());
    const VkDescriptorSet descriptor_set = context.copy_address_descriptor_set->getHandle();
    vkCmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        context.copy_address_pipeline_layout->getHandle(),
        0,
        1,
        &descriptor_set,
        0,
        nullptr
    );
    vkCmdPushConstants(
        command_buffer,
        context.copy_address_pipeline_layout->getHandle(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(element_count),
        &element_count
    );
    vkCmdDispatch(command_buffer, group_count_x, 1, 1);

    check(context.m_defaultCommandBuffer->end());
    context.submit(context.m_defaultQueue, {context.m_defaultCommandBuffer}, VK_NULL_HANDLE, {}, {}, false);
    check(vkQueueWaitIdle(context.m_defaultQueue));
}

static void populate_process_instance_shader_inputs(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    const VkDeviceSize transforms_size = context.process_instance_transforms_buffer->getSize();
    check(context.process_instance_transforms_buffer->map(0, transforms_size));
    auto* transforms = static_cast<TransformRow*>(context.process_instance_transforms_buffer->m_mappedAddress);
    for (uint32_t as_index = 0; as_index < bl_as_create_count; ++as_index)
    {
        transforms[as_index * 3 + 0] = {{1.0f, 0.0f, 0.0f, 0.0f}};
        transforms[as_index * 3 + 1] = {{0.0f, 1.0f, 0.0f, 0.0f}};
        transforms[as_index * 3 + 2] = {{0.0f, 0.0f, 1.0f, 0.0f}};
    }
    if (vulkan.has_explicit_host_updates)
    {
        context.process_instance_transforms_buffer->flush(true);
    }
    context.process_instance_transforms_buffer->unmap();
}

static void dispatch_process_instance_pipeline(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    assert(context.process_instance_pipeline);
    assert(context.process_instance_descriptor_set);
    assert(context.instance_buffer);

    const uint32_t num_instances = bl_as_create_count;
    const uint32_t group_count_x = (num_instances + local_size_x - 1) / local_size_x;
    assert(group_count_x > 0);

    VkCommandBuffer command_buffer = context.m_defaultCommandBuffer->getHandle();
    check(vkResetCommandBuffer(command_buffer, 0));
    check(context.m_defaultCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
    context.m_defaultCommandBuffer->bufferMemoryBarrier(
        *context.blas_address_words_buffer,
        0,
        context.blas_address_words_buffer->getSize(),
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.process_instance_pipeline->getHandle());
    const VkDescriptorSet descriptor_set = context.process_instance_descriptor_set->getHandle();
    vkCmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        context.process_instance_pipeline_layout->getHandle(),
        0,
        1,
        &descriptor_set,
        0,
        nullptr
    );
    vkCmdPushConstants(
        command_buffer,
        context.process_instance_pipeline_layout->getHandle(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(num_instances),
        &num_instances
    );
    vkCmdDispatch(command_buffer, group_count_x, 1, 1);
    check(context.m_defaultCommandBuffer->end());
    context.submit(context.m_defaultQueue, {context.m_defaultCommandBuffer}, VK_NULL_HANDLE, {}, {}, false);
    check(vkQueueWaitIdle(context.m_defaultQueue));
}

[[maybe_unused]] static void populate_top_level_instance_buffer_host(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    static const VkTransformMatrixKHR identity = {{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f}
    }};

    assert(context.instance_buffer);
    const VkDeviceSize instance_buffer_size = context.instance_buffer->getSize();
    check(context.instance_buffer->map(0, instance_buffer_size));
    auto* instances = static_cast<VkAccelerationStructureInstanceKHR*>(context.instance_buffer->m_mappedAddress);
    for (uint32_t as_index = 0; as_index < bl_as_create_count; ++as_index)
    {
        instances[as_index] = {};
        instances[as_index].transform = identity;
        instances[as_index].instanceCustomIndex = as_index;
        instances[as_index].mask = 0xFF;
        instances[as_index].instanceShaderBindingTableRecordOffset = 0;
        instances[as_index].flags = 0;
        instances[as_index].accelerationStructureReference = context.backed_bl_acc_structures[as_index].as.address.deviceAddress;
    }
    if (vulkan.has_trace_helpers)
    {
        std::vector<VkDeviceSize> marked_offsets(bl_as_create_count);
        for (uint32_t as_index = 0; as_index < bl_as_create_count; ++as_index)
        {
            marked_offsets[as_index] = as_index * sizeof(VkAccelerationStructureInstanceKHR) +
                                       offsetof(VkAccelerationStructureInstanceKHR, accelerationStructureReference);
        }
        testFlushMemoryDeviceAddresses(
            vulkan,
            context.instance_buffer->getMemory(),
            0,
            instance_buffer_size,
            marked_offsets,
            VK_DEVICE_ADDRESS_TYPE_ACCELERATION_STRUCTURE_ARM,
            true
        );
    }
    else if (vulkan.has_explicit_host_updates)
    {
        context.instance_buffer->flush(true);
    }
    context.instance_buffer->unmap();
}

static void create_build_top_level_acceleration_structure(const vulkan_setup_t& vulkan, AsPopulateInstanceBufferContext& context)
{
    assert(!context.backed_bl_acc_structures.empty());
    assert(context.functions.vkCreateAccelerationStructureKHR);
    assert(context.functions.vkGetAccelerationStructureBuildSizesKHR);
    assert(context.functions.vkGetAccelerationStructureDeviceAddressKHR);
    assert(context.functions.vkCmdBuildAccelerationStructuresKHR);

    assert(context.instance_buffer);
    // Size and create the TLAS from the instance buffer, then allocate a dedicated scratch buffer.
    VkAccelerationStructureGeometryInstancesDataKHR instance_data{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR, nullptr};
    instance_data.arrayOfPointers = VK_FALSE;
    instance_data.data.deviceAddress = context.instance_buffer->getBufferDeviceAddress();
    assert(instance_data.data.deviceAddress != 0);

    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instance_data;

    VkAccelerationStructureBuildGeometryInfoKHR build_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info.geometryCount = 1;
    build_info.pGeometries = &geometry;
    build_info.srcAccelerationStructure = VK_NULL_HANDLE;

    VkAccelerationStructureBuildRangeInfoKHR range_info{};
    range_info.primitiveCount = bl_as_create_count;
    range_info.primitiveOffset = 0;
    range_info.firstVertex = 0;
    range_info.transformOffset = 0;

    VkAccelerationStructureBuildSizesInfoKHR build_size_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
    context.functions.vkGetAccelerationStructureBuildSizesKHR(
        vulkan.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build_info,
        &range_info.primitiveCount,
        &build_size_info
    );
    assert(build_size_info.accelerationStructureSize > 0);
    assert(build_size_info.buildScratchSize > 0);

    const BackedAccelerationStructure created_tlas =
        acceleration_structures::create_acceleration_structure(
            vulkan,
            context.functions,
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            build_size_info.accelerationStructureSize
        );
    context.backed_tl_acc_structure = created_tlas;

    AsBuffer scratch_buffer = acceleration_structures::prepare_buffer(
        vulkan,
        build_size_info.buildScratchSize,
        nullptr,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info.dstAccelerationStructure = context.backed_tl_acc_structure.as.handle;
	build_info.scratchData.deviceAddress = scratch_buffer.address.deviceAddress;

    // Record and submit one device-side TLAS build using the prepared instance buffer contents.
    VkCommandBuffer command_buffer = context.m_defaultCommandBuffer->getHandle();
    VkAccelerationStructureBuildRangeInfoKHR* range_infos = &range_info;
    check(vkResetCommandBuffer(command_buffer, 0));
    check(context.m_defaultCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
    context.m_defaultCommandBuffer->bufferMemoryBarrier(
        *context.instance_buffer,
        0,
        context.instance_buffer->getSize(),
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    );
    context.functions.vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &build_info, &range_infos);
    check(context.m_defaultCommandBuffer->end());
    context.submit(context.m_defaultQueue, {context.m_defaultCommandBuffer}, VK_NULL_HANDLE, {}, {}, false);
    check(vkQueueWaitIdle(context.m_defaultQueue));

    context.backed_tl_acc_structure.as.address.deviceAddress =
        acceleration_structures::get_acceleration_structure_device_address(
            vulkan,
            context.functions,
            context.backed_tl_acc_structure.as.handle
        );
    assert(context.backed_tl_acc_structure.as.address.deviceAddress != 0);

    acceleration_structures::destroy_buffer(vulkan, scratch_buffer);
}

static void verify_blas_address_words_buffer(AsPopulateInstanceBufferContext& context)
{
    if (get_env_int("TOOLSTEST_NULL_RUN", 0))
    {
        printf("  skipping BLAS address buffer output verification for null run\n");
        return;
    }

    VkCommandBuffer command_buffer = context.m_defaultCommandBuffer->getHandle();
    check(vkResetCommandBuffer(command_buffer, 0));
    check(context.m_defaultCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
    context.m_defaultCommandBuffer->bufferMemoryBarrier(
        *context.blas_address_words_buffer,
        0,
        context.blas_address_words_buffer->getSize(),
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_HOST_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT
    );
    check(context.m_defaultCommandBuffer->end());
    context.submit(context.m_defaultQueue, {context.m_defaultCommandBuffer}, VK_NULL_HANDLE, {}, {}, false);
    check(vkQueueWaitIdle(context.m_defaultQueue));

    const VkDeviceSize buffer_size = sizeof(PackedAddressPair) * get_copy_address_element_count();
    check(context.blas_address_words_buffer->map(0, buffer_size));

    const auto* copied_addresses = static_cast<const PackedAddressPair*>(context.blas_address_words_buffer->m_mappedAddress);
    for (uint32_t as_index = 0; as_index < bl_as_create_count; ++as_index)
    {
        const uint32_t packed_index = as_index / 2;
        const uint32_t packed_slot = as_index % 2;
        VkDeviceAddress copied_address = read_packed_address(copied_addresses[packed_index], packed_slot);
        assert(copied_address == context.backed_bl_acc_structures[as_index].as.address.deviceAddress);
    }

    context.blas_address_words_buffer->unmap();
}

static void verify_instance_buffer_acceleration_structure_references(AsPopulateInstanceBufferContext& context)
{
    if (get_env_int("TOOLSTEST_NULL_RUN", 0))
    {
        printf("  skipping instance buffer output verification for null run\n");
        return;
    }

    VkCommandBuffer command_buffer = context.m_defaultCommandBuffer->getHandle();
    check(vkResetCommandBuffer(command_buffer, 0));
    check(context.m_defaultCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
    context.m_defaultCommandBuffer->bufferMemoryBarrier(
        *context.instance_buffer,
        0,
        context.instance_buffer->getSize(),
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_HOST_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT
    );
    check(context.m_defaultCommandBuffer->end());
    context.submit(context.m_defaultQueue, {context.m_defaultCommandBuffer}, VK_NULL_HANDLE, {}, {}, false);
    check(vkQueueWaitIdle(context.m_defaultQueue));

    const VkDeviceSize instance_buffer_size = context.instance_buffer->getSize();
    check(context.instance_buffer->map(0, instance_buffer_size));
    const auto* instances = static_cast<const VkAccelerationStructureInstanceKHR*>(context.instance_buffer->m_mappedAddress);
    for (uint32_t as_index = 0; as_index < bl_as_create_count; ++as_index)
    {
        assert(instances[as_index].accelerationStructureReference == context.backed_bl_acc_structures[as_index].as.address.deviceAddress);
    }
    context.instance_buffer->unmap();
}


int main(int argc, char** argv)
{
    vulkan_req_t reqs{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR required_acc_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        nullptr,
        VK_TRUE
    };
    reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
    reqs.bufferDeviceAddress = true;
    reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&required_acc_features);
    reqs.apiVersion = VK_API_VERSION_1_2;
    reqs.queues = 1;
    reqs.usage = show_usage;
    reqs.cmdopt = test_cmdopt;

    vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_as_populate_instance_buffer", reqs);
    print_acceleration_structure_features(vulkan);

    p_test = std::make_unique<AsPopulateInstanceBufferContext>();
    check(p_test->initBasic(vulkan, reqs));
    prepare_test_resources(vulkan, *p_test);

    bench_start_iteration(vulkan.bench);
    create_build_bottom_level_acceleration_structures(vulkan, *p_test);
    populate_copy_address_shader_inputs(vulkan, *p_test);
    dispatch_copy_address_pipeline(vulkan, *p_test);
    verify_blas_address_words_buffer(*p_test);

    populate_process_instance_shader_inputs(vulkan, *p_test);
    dispatch_process_instance_pipeline(vulkan, *p_test);
    verify_instance_buffer_acceleration_structure_references(*p_test);
    create_build_top_level_acceleration_structure(vulkan, *p_test);
    bench_stop_iteration(vulkan.bench);

    check(vkDeviceWaitIdle(vulkan.device));
    p_test = nullptr;
    return 0;
}
