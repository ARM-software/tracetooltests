#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

#include <cstdint>
#include <cstring>
#include <memory>

// glslangValidator -V vulkan_as_scatter_upload.comp -o
// vulkan_as_scatter_upload.spirv --target-env vulkan1.2
// xxd -i vulkan_as_scatter_upload.spirv vulkan_as_scatter_upload.inc
#include "vulkan_as_scatter_upload.inc"

// glslangValidator -V vulkan_as_scatter_upload_instance.comp -o
// vulkan_as_scatter_upload_instance.spirv --target-env vulkan1.2
// xxd -i vulkan_as_scatter_upload_instance.spirv
// vulkan_as_scatter_upload_instance.inc
#include "vulkan_as_scatter_upload_instance.inc"

using AsBuffer = acceleration_structures::Buffer;
using BackedAccelerationStructure = acceleration_structures::BackedAccelerationStructure;
using namespace tracetooltests;

static constexpr uint32_t kSourceWordCount = 64;
static constexpr uint32_t kSourceAddressWordOffset = 62;
static constexpr uint32_t kSceneWordCount = 4096;
static constexpr uint32_t kSceneAddressWordOffset = 4094;
static constexpr VkDeviceSize kReadbackInstanceOffset = sizeof(VkDeviceAddress);
static constexpr VkDeviceSize kReadbackCounterOffset =
	kReadbackInstanceOffset + sizeof(VkAccelerationStructureInstanceKHR);
static constexpr VkDeviceSize kReadbackSize = kReadbackCounterOffset + sizeof(uint32_t);

struct Vertex
{
	float pos[3];
};

struct ScatterPatch
{
	uint32_t source_word_offset;
	uint32_t destination_word_offset;
	uint32_t word_count;
	uint32_t unused;
};
static_assert(sizeof(ScatterPatch) == 4 * sizeof(uint32_t));

class ScatterUploadContext : public GraphicContext
{
  public:
	ScatterUploadContext() : GraphicContext() {}
	~ScatterUploadContext() { destroy(); }

	void destroy() override
	{
		scatter_pipeline = nullptr;
		scatter_pipeline_layout = nullptr;
		scatter_descriptor_set = nullptr;
		scatter_descriptor_set_layout = nullptr;
		instance_pipeline = nullptr;
		instance_pipeline_layout = nullptr;
		instance_descriptor_set = nullptr;
		instance_descriptor_set_layout = nullptr;

		patch_buffer = nullptr;
		source_buffer = nullptr;
		scene_buffer = nullptr;
		counter_buffer = nullptr;
		instance_buffer = nullptr;
		readback_buffer = nullptr;

		if (m_vulkanSetup.device != VK_NULL_HANDLE)
		{
			acceleration_structures::destroy_backed_acceleration_structure(m_vulkanSetup, functions, tlas);
			acceleration_structures::destroy_backed_acceleration_structure(m_vulkanSetup, functions, blas);
		}
	}

	acceleration_structures::functions functions;
	BackedAccelerationStructure blas;
	BackedAccelerationStructure tlas;

	std::unique_ptr<Buffer> patch_buffer;
	std::unique_ptr<Buffer> source_buffer;
	std::unique_ptr<Buffer> scene_buffer;
	std::unique_ptr<Buffer> counter_buffer;
	std::unique_ptr<Buffer> instance_buffer;
	std::unique_ptr<Buffer> readback_buffer;

	std::unique_ptr<DescriptorSetLayout> scatter_descriptor_set_layout;
	std::unique_ptr<DescriptorSet> scatter_descriptor_set;
	std::unique_ptr<PipelineLayout> scatter_pipeline_layout;
	std::unique_ptr<ComputePipeline> scatter_pipeline;

	std::unique_ptr<DescriptorSetLayout> instance_descriptor_set_layout;
	std::unique_ptr<DescriptorSet> instance_descriptor_set;
	std::unique_ptr<PipelineLayout> instance_pipeline_layout;
	std::unique_ptr<ComputePipeline> instance_pipeline;
};

static std::unique_ptr<ScatterUploadContext> p_test = nullptr;

static void show_usage()
{
	printf("Build a TLAS from a device-local instance populated through an Anki-style GPU scatter upload.\n");
}

static bool test_cmdopt(int &i, int argc, char **argv, vulkan_req_t &reqs)
{
	(void)i;
	(void)argc;
	(void)argv;
	(void)reqs;
	return false;
}

static void create_buffers(const vulkan_setup_t &vulkan, ScatterUploadContext &context)
{
	context.patch_buffer = std::make_unique<Buffer>(vulkan);
	check(context.patch_buffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(ScatterPatch),
	                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

	context.source_buffer = std::make_unique<Buffer>(vulkan);
	check(context.source_buffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kSourceWordCount * sizeof(uint32_t),
	                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

	context.scene_buffer = std::make_unique<Buffer>(vulkan);
	check(context.scene_buffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                                   kSceneWordCount * sizeof(uint32_t), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

	context.counter_buffer = std::make_unique<Buffer>(vulkan);
	check(context.counter_buffer->create(
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, sizeof(uint32_t),
	    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

	context.instance_buffer = std::make_unique<Buffer>(vulkan);
	check(context.instance_buffer->create(
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
	        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    sizeof(VkAccelerationStructureInstanceKHR), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

	context.readback_buffer = std::make_unique<Buffer>(vulkan);
	check(context.readback_buffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT, kReadbackSize,
	                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
}

static void create_scatter_pipeline(const vulkan_setup_t &vulkan, ScatterUploadContext &context)
{
	context.scatter_descriptor_set_layout = std::make_unique<DescriptorSetLayout>(vulkan.device);
	context.scatter_descriptor_set_layout->insertBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
	context.scatter_descriptor_set_layout->insertBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
	context.scatter_descriptor_set_layout->insertBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
	check(context.scatter_descriptor_set_layout->create());

	auto descriptor_pool = std::make_unique<DescriptorSetPool>(vulkan.device);
	check(descriptor_pool->create(1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}}));
	context.scatter_descriptor_set = std::make_unique<DescriptorSet>(std::move(descriptor_pool));
	check(context.scatter_descriptor_set->create(*context.scatter_descriptor_set_layout));
	context.scatter_descriptor_set->setBuffer(0, 0, *context.patch_buffer);
	context.scatter_descriptor_set->setBuffer(1, 0, *context.source_buffer);
	context.scatter_descriptor_set->setBuffer(2, 0, *context.scene_buffer);
	context.scatter_descriptor_set->update();

	context.scatter_pipeline_layout = std::make_unique<PipelineLayout>(vulkan.device);
	check(context.scatter_pipeline_layout->create({context.scatter_descriptor_set_layout->getHandle()}, {}));
	auto shader = std::make_unique<Shader>(vulkan.device);
	check(shader->create(vulkan_as_scatter_upload_spirv, vulkan_as_scatter_upload_spirv_len));
	ShaderPipelineState shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, std::move(shader));
	context.scatter_pipeline = std::make_unique<ComputePipeline>(vulkan.device);
	check(context.scatter_pipeline->create(context.scatter_pipeline_layout->getHandle(), shader_stage));
}

static void create_instance_pipeline(const vulkan_setup_t &vulkan, ScatterUploadContext &context)
{
	context.instance_descriptor_set_layout = std::make_unique<DescriptorSetLayout>(vulkan.device);
	context.instance_descriptor_set_layout->insertBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
	context.instance_descriptor_set_layout->insertBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
	context.instance_descriptor_set_layout->insertBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
	check(context.instance_descriptor_set_layout->create());

	auto descriptor_pool = std::make_unique<DescriptorSetPool>(vulkan.device);
	check(descriptor_pool->create(1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}}));
	context.instance_descriptor_set = std::make_unique<DescriptorSet>(std::move(descriptor_pool));
	check(context.instance_descriptor_set->create(*context.instance_descriptor_set_layout));
	context.instance_descriptor_set->setBuffer(0, 0, *context.instance_buffer);
	context.instance_descriptor_set->setBuffer(1, 0, *context.scene_buffer);
	context.instance_descriptor_set->setBuffer(2, 0, *context.counter_buffer);
	context.instance_descriptor_set->update();

	context.instance_pipeline_layout = std::make_unique<PipelineLayout>(vulkan.device);
	check(context.instance_pipeline_layout->create(
	    {context.instance_descriptor_set_layout->getHandle()}, {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)}}));
	auto shader = std::make_unique<Shader>(vulkan.device);
	check(shader->create(vulkan_as_scatter_upload_instance_spirv, vulkan_as_scatter_upload_instance_spirv_len));
	ShaderPipelineState shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, std::move(shader));
	context.instance_pipeline = std::make_unique<ComputePipeline>(vulkan.device);
	check(context.instance_pipeline->create(context.instance_pipeline_layout->getHandle(), shader_stage));
}

static void build_bottom_level_acceleration_structure(const vulkan_setup_t &vulkan, ScatterUploadContext &context)
{
	constexpr Vertex triangle_vertices[] = {
	    {{0.0f, -0.5f, 0.0f}},
	    {{0.5f, 0.5f, 0.0f}},
	    {{-0.5f, 0.5f, 0.0f}},
	};
	constexpr uint32_t primitive_count = 1;

	AsBuffer vertex_buffer = acceleration_structures::prepare_buffer(
	    vulkan, sizeof(triangle_vertices), triangle_vertices,
	    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkAccelerationStructureGeometryTrianglesDataKHR triangles{
	    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR, nullptr};
	triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	triangles.vertexData = vertex_buffer.address;
	triangles.vertexStride = sizeof(Vertex);
	triangles.maxVertex = 2;
	triangles.indexType = VK_INDEX_TYPE_NONE_KHR;

	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles = triangles;

	VkAccelerationStructureBuildGeometryInfoKHR build_info{
	    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info.geometryCount = 1;
	build_info.pGeometries = &geometry;

	VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	context.functions.vkGetAccelerationStructureBuildSizesKHR(vulkan.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
	                                                          &build_info, &primitive_count, &sizes);
	assert(sizes.accelerationStructureSize > 0);
	assert(sizes.buildScratchSize > 0);

	context.blas = acceleration_structures::create_acceleration_structure(
	    vulkan, context.functions, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize);
	AsBuffer scratch = acceleration_structures::prepare_buffer(
	    vulkan, sizes.buildScratchSize, nullptr, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	build_info.dstAccelerationStructure = context.blas.as.handle;
	build_info.scratchData.deviceAddress = scratch.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR range{};
	range.primitiveCount = primitive_count;
	const VkAccelerationStructureBuildRangeInfoKHR *range_ptr = &range;
	VkCommandBuffer command_buffer = context.m_defaultCommandBuffer->getHandle();
	check(vkResetCommandBuffer(command_buffer, 0));
	check(context.m_defaultCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
	context.functions.vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &build_info, &range_ptr);
	check(context.m_defaultCommandBuffer->end());
	context.submit(context.m_defaultQueue, {context.m_defaultCommandBuffer}, VK_NULL_HANDLE, {}, {}, false);
	check(vkQueueWaitIdle(context.m_defaultQueue));

	context.blas.as.address.deviceAddress = acceleration_structures::get_acceleration_structure_device_address(
	    vulkan, context.functions, context.blas.as.handle);
	assert(context.blas.as.address.deviceAddress != 0);
	acceleration_structures::destroy_buffer(vulkan, scratch);
	acceleration_structures::destroy_buffer(vulkan, vertex_buffer);
}

static void populate_upload_inputs(const vulkan_setup_t &vulkan, ScatterUploadContext &context)
{
	ScatterPatch patch{kSourceAddressWordOffset, kSceneAddressWordOffset, 2, 0};
	check(context.patch_buffer->map());
	std::memcpy(context.patch_buffer->m_mappedAddress, &patch, sizeof(patch));
	if (vulkan.has_explicit_host_updates)
	{
		context.patch_buffer->flush(true);
	}
	context.patch_buffer->unmap();

	uint32_t source_words[kSourceWordCount]{};
	std::memcpy(&source_words[kSourceAddressWordOffset], &context.blas.as.address.deviceAddress, sizeof(VkDeviceAddress));
	check(context.source_buffer->map());
	std::memcpy(context.source_buffer->m_mappedAddress, source_words, sizeof(source_words));
	if (vulkan.has_trace_helpers)
	{
		std::vector<VkDeviceSize> marked_offsets = {kSourceAddressWordOffset * sizeof(uint32_t)};
		testFlushMemoryDeviceAddresses(vulkan, context.source_buffer->getMemory(), 0, sizeof(source_words), marked_offsets,
		                               VK_DEVICE_ADDRESS_TYPE_ACCELERATION_STRUCTURE_ARM, true);
	}
	else if (vulkan.has_explicit_host_updates)
	{
		context.source_buffer->flush(true);
	}
	context.source_buffer->unmap();
}

static VkBufferMemoryBarrier buffer_barrier(VkBuffer buffer, VkAccessFlags source_access, VkAccessFlags destination_access)
{
	VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
	barrier.srcAccessMask = source_access;
	barrier.dstAccessMask = destination_access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;
	return barrier;
}

static void run_scatter_and_build_tlas(const vulkan_setup_t &vulkan, ScatterUploadContext &context)
{
	VkAccelerationStructureGeometryInstancesDataKHR instances{
	    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR, nullptr};
	instances.arrayOfPointers = VK_FALSE;
	instances.data.deviceAddress = context.instance_buffer->getBufferDeviceAddress();

	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = instances;

	VkAccelerationStructureBuildGeometryInfoKHR build_info{
	    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info.geometryCount = 1;
	build_info.pGeometries = &geometry;

	const uint32_t primitive_count = 1;
	VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	context.functions.vkGetAccelerationStructureBuildSizesKHR(vulkan.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
	                                                          &build_info, &primitive_count, &sizes);
	assert(sizes.accelerationStructureSize > 0);
	assert(sizes.buildScratchSize > 0);
	context.tlas = acceleration_structures::create_acceleration_structure(
	    vulkan, context.functions, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizes.accelerationStructureSize);
	AsBuffer scratch = acceleration_structures::prepare_buffer(
	    vulkan, sizes.buildScratchSize, nullptr, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	build_info.dstAccelerationStructure = context.tlas.as.handle;
	build_info.scratchData.deviceAddress = scratch.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR range{};
	range.primitiveCount = primitive_count;
	const VkAccelerationStructureBuildRangeInfoKHR *range_ptr = &range;

	VkCommandBuffer command_buffer = context.m_defaultCommandBuffer->getHandle();
	check(vkResetCommandBuffer(command_buffer, 0));
	check(context.m_defaultCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));

	vkCmdFillBuffer(command_buffer, context.counter_buffer->getHandle(), 0, sizeof(uint32_t), 0);
	VkBufferMemoryBarrier counter_fill = buffer_barrier(context.counter_buffer->getHandle(), VK_ACCESS_TRANSFER_WRITE_BIT,
	                                                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
	                     &counter_fill, 0, nullptr);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.scatter_pipeline->getHandle());
	VkDescriptorSet scatter_set = context.scatter_descriptor_set->getHandle();
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.scatter_pipeline_layout->getHandle(), 0, 1,
	                        &scatter_set, 0, nullptr);
	vkCmdDispatch(command_buffer, 1, 1, 1);

	VkBufferMemoryBarrier scene_ready = buffer_barrier(context.scene_buffer->getHandle(), VK_ACCESS_SHADER_WRITE_BIT,
	                                                   VK_ACCESS_SHADER_READ_BIT);
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
	                     &scene_ready, 0, nullptr);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.instance_pipeline->getHandle());
	VkDescriptorSet instance_set = context.instance_descriptor_set->getHandle();
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.instance_pipeline_layout->getHandle(), 0, 1,
	                        &instance_set, 0, nullptr);
	vkCmdPushConstants(command_buffer, context.instance_pipeline_layout->getHandle(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
	                   sizeof(kSceneAddressWordOffset), &kSceneAddressWordOffset);
	vkCmdDispatch(command_buffer, 1, 1, 1);

	VkBufferMemoryBarrier outputs_ready[] = {
	    buffer_barrier(context.scene_buffer->getHandle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT),
	    buffer_barrier(context.instance_buffer->getHandle(), VK_ACCESS_SHADER_WRITE_BIT,
	                   VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_TRANSFER_READ_BIT),
	    buffer_barrier(context.counter_buffer->getHandle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT),
	};
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                     VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 3,
	                     outputs_ready, 0, nullptr);
	context.functions.vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &build_info, &range_ptr);

	VkBufferCopy copy{};
	copy.srcOffset = kSceneAddressWordOffset * sizeof(uint32_t);
	copy.dstOffset = 0;
	copy.size = sizeof(VkDeviceAddress);
	vkCmdCopyBuffer(command_buffer, context.scene_buffer->getHandle(), context.readback_buffer->getHandle(), 1, &copy);
	copy.srcOffset = 0;
	copy.dstOffset = kReadbackInstanceOffset;
	copy.size = sizeof(VkAccelerationStructureInstanceKHR);
	vkCmdCopyBuffer(command_buffer, context.instance_buffer->getHandle(), context.readback_buffer->getHandle(), 1, &copy);
	copy.dstOffset = kReadbackCounterOffset;
	copy.size = sizeof(uint32_t);
	vkCmdCopyBuffer(command_buffer, context.counter_buffer->getHandle(), context.readback_buffer->getHandle(), 1, &copy);

	VkBufferMemoryBarrier readback_ready = buffer_barrier(context.readback_buffer->getHandle(), VK_ACCESS_TRANSFER_WRITE_BIT,
	                                                      VK_ACCESS_HOST_READ_BIT);
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
	                     &readback_ready, 0, nullptr);
	check(context.m_defaultCommandBuffer->end());
	context.submit(context.m_defaultQueue, {context.m_defaultCommandBuffer}, VK_NULL_HANDLE, {}, {}, false);
	check(vkQueueWaitIdle(context.m_defaultQueue));

	context.tlas.as.address.deviceAddress = acceleration_structures::get_acceleration_structure_device_address(
	    vulkan, context.functions, context.tlas.as.handle);
	assert(context.tlas.as.address.deviceAddress != 0);
	acceleration_structures::destroy_buffer(vulkan, scratch);
}

static void verify_results(const vulkan_setup_t &vulkan, ScatterUploadContext &context)
{
	if (get_env_int("TOOLSTEST_NULL_RUN", 0))
	{
		printf("  skipping scatter upload output verification for null run\n");
		return;
	}

	check(context.readback_buffer->map());
	const auto *bytes = static_cast<const uint8_t *>(context.readback_buffer->m_mappedAddress);
	VkDeviceAddress scattered_address = 0;
	VkAccelerationStructureInstanceKHR instance{};
	uint32_t counter = 0;
	std::memcpy(&scattered_address, bytes, sizeof(scattered_address));
	std::memcpy(&instance, bytes + kReadbackInstanceOffset, sizeof(instance));
	std::memcpy(&counter, bytes + kReadbackCounterOffset, sizeof(counter));
	assert(scattered_address == context.blas.as.address.deviceAddress);
	assert(instance.accelerationStructureReference == context.blas.as.address.deviceAddress);
	assert(counter == 1);
	context.readback_buffer->unmap();

	if (vulkan.vkAssertBuffer)
	{
		uint32_t checksum = 0;
		VkUpdateBufferInfoARM info{VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, context.instance_buffer->getHandle(), 0,
		                           context.instance_buffer->getSize(), nullptr};
		VkResult result = vulkan.vkAssertBuffer(vulkan.device, &info, &checksum, "scatter-uploaded TLAS instance buffer");
		assert(result == VK_SUCCESS || result == VK_INCOMPLETE);
	}
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, nullptr, VK_TRUE};
	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
	reqs.bufferDeviceAddress = true;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&acceleration_features);
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.queues = 1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_as_scatter_upload", reqs);
	p_test = std::make_unique<ScatterUploadContext>();
	check(p_test->initBasic(vulkan, reqs));
	p_test->functions = acceleration_structures::query_acceleration_structure_functions(vulkan);

	create_buffers(vulkan, *p_test);
	create_scatter_pipeline(vulkan, *p_test);
	create_instance_pipeline(vulkan, *p_test);
	build_bottom_level_acceleration_structure(vulkan, *p_test);
	populate_upload_inputs(vulkan, *p_test);

	bench_start_iteration(vulkan.bench);
	run_scatter_and_build_tlas(vulkan, *p_test);
	bench_stop_iteration(vulkan.bench);
	verify_results(vulkan, *p_test);

	check(vkDeviceWaitIdle(vulkan.device));
	p_test = nullptr;
	return 0;
}
