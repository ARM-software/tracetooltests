// Vulkan test for device-address-based remapping into a target buffer.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

#include "vulkan_address_remap_test_1.inc"

#include <cstring>

using namespace tracetooltests;

namespace
{
constexpr uint32_t kOffsetCount = 128;
constexpr uint32_t kOutputCount = 256;
constexpr uint32_t kWorkgroupSize = 64;
constexpr uint32_t kStride = 7;
constexpr uint32_t kFillValue = 0x5a5aa5a5u;
}

struct PushConstants
{
	uint32_t base_lo;
	uint32_t base_hi;
	uint32_t count;
	uint32_t value;
};

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_address_remap_test_1", reqs);
	int ret = 0;

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	const VkDeviceSize offsets_size = kOffsetCount * sizeof(uint32_t);
	const VkDeviceSize output_size = kOutputCount * sizeof(uint32_t);

	Buffer offsets_buffer(vulkan);
	Buffer output_buffer(vulkan);
	offsets_buffer.create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, offsets_size,
	                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	output_buffer.create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, output_size,
	                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	const VkDeviceAddress output_base = output_buffer.getBufferDeviceAddress();

	std::vector<uint32_t> offsets(kOffsetCount, 0);
	std::vector<uint32_t> expected(kOutputCount, 0);
	for (uint32_t i = 0; i < kOffsetCount; ++i)
	{
		const uint32_t index = (i * kStride) % kOutputCount;
		const VkDeviceAddress address = output_base + static_cast<VkDeviceAddress>(index * sizeof(uint32_t));
		offsets[i] = static_cast<uint32_t>(address - output_base);
		expected[index] = kFillValue;
	}

	offsets_buffer.map();
	output_buffer.map();
	std::memset(offsets_buffer.m_mappedAddress, 0, offsets_buffer.getSize());
	std::memset(output_buffer.m_mappedAddress, 0, output_buffer.getSize());
	std::memcpy(offsets_buffer.m_mappedAddress, offsets.data(), offsets.size() * sizeof(uint32_t));
	offsets_buffer.flush(false);
	output_buffer.flush(false);
	offsets_buffer.unmap();
	output_buffer.unmap();

	PushConstants push_constants{};
	push_constants.base_lo = static_cast<uint32_t>(output_base & 0xffffffffu);
	push_constants.base_hi = static_cast<uint32_t>(output_base >> 32);
	push_constants.count = kOffsetCount;
	push_constants.value = kFillValue;

	auto set_layout = std::make_shared<DescriptorSetLayout>(vulkan.device);
	set_layout->insertBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
	set_layout->create();

	auto set_pool = std::make_shared<DescriptorSetPool>(set_layout);
	set_pool->create(1);

	auto descriptor_set = std::make_shared<DescriptorSet>(set_pool);
	descriptor_set->create();
	descriptor_set->setBuffer(0, offsets_buffer);
	descriptor_set->update();

	std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> layout_map = {{0, set_layout}};
	VkPushConstantRange push_range{};
	push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_range.offset = 0;
	push_range.size = sizeof(PushConstants);

	auto pipeline_layout = std::make_shared<PipelineLayout>(vulkan.device);
	pipeline_layout->create(layout_map, {push_range});

	auto shader = std::make_shared<Shader>(vulkan.device);
	shader->create(vulkan_address_remap_test_1_spirv, vulkan_address_remap_test_1_spirv_len);
	ShaderPipelineState shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, shader);

	auto pipeline = std::make_shared<ComputePipeline>(pipeline_layout);
	pipeline->create(shader_stage);
	shader_stage.destroy();
	shader_stage.m_pShader.reset();

	auto command_pool = std::make_shared<CommandBufferPool>(vulkan.device);
	command_pool->create(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0);

	auto command_buffer = std::make_shared<CommandBuffer>(command_pool);
	command_buffer->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	command_buffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	command_buffer->bufferMemoryBarrier(offsets_buffer, 0, offsets_buffer.getSize(),
	                                    VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
	                                    VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	command_buffer->bufferMemoryBarrier(output_buffer, 0, output_buffer.getSize(),
	                                    VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	                                    VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	VkCommandBuffer cmd = command_buffer->getHandle();
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getHandle());
	VkDescriptorSet set_handle = descriptor_set->getHandle();
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout->getHandle(), 0, 1, &set_handle, 0, nullptr);
	vkCmdPushConstants(cmd, pipeline_layout->getHandle(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &push_constants);
	const uint32_t group_count = (kOffsetCount + kWorkgroupSize - 1) / kWorkgroupSize;
	vkCmdDispatch(cmd, group_count, 1, 1);
	command_buffer->bufferMemoryBarrier(output_buffer, 0, output_buffer.getSize(),
	                                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
	                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT);

	VkResult result = command_buffer->end();
	check(result);

	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd;

	bench_start_iteration(vulkan.bench);
	result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(queue);
	check(result);
	bench_stop_iteration(vulkan.bench);

	output_buffer.map();
	const uint32_t* output_data = reinterpret_cast<const uint32_t*>(output_buffer.m_mappedAddress);
	bool ok = true;
	for (uint32_t i = 0; i < kOutputCount; ++i)
	{
		if (output_data[i] != expected[i])
		{
			printf("address_remap_test_1 mismatch at %u: got 0x%08x expected 0x%08x\n",
			        i, output_data[i], expected[i]);
			ok = false;
			break;
		}
	}
	output_buffer.unmap();

	if (ok)
	{
		printf("address_remap_test_1 verified %u writes\n", kOffsetCount);
		if (vulkan.vkAssertBuffer)
		{
			uint32_t crc = 0;
			ret = vulkan.vkAssertBuffer(vulkan.device, output_buffer.getHandle(), 0, VK_WHOLE_SIZE, &crc, "output buffer");
			assert(ret == VK_SUCCESS);
		}
	}
	else
	{
		ret = 1;
	}

	descriptor_set.reset();
	set_pool.reset();
	pipeline.reset();
	shader.reset();
	layout_map.clear(); // drop shared_ptr ref before destroying device
	pipeline_layout.reset();
	set_layout.reset();
	command_buffer.reset();
	command_pool.reset();
	output_buffer.destroy();
	offsets_buffer.destroy();

	test_done(vulkan);
	return ret;
}
