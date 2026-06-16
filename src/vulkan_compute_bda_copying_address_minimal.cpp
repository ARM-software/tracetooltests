// Reduced test for copying buffer device addresses through shader memory.

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

#include <array>
#include <cmath>
#include <cstring>

#include "vulkan_compute_bda_copying_address_interleave.inc"
#include "vulkan_compute_bda_copying_address_output.inc"

struct pushAddress
{
	VkDeviceAddress BDA;
	VkDeviceAddress colorBDA;
	VkDeviceAddress addressBDA;
};

static void push_address_constants(VkCommandBuffer commandBuffer, const vulkan_setup_t& vulkan,
	VkPipelineLayout layout, const pushAddress& push, uint32_t addressCount, VkDeviceSize sizeBytes)
{
	assert(addressCount <= 3);
	if (!vulkan.has_trace_helpers)
	{
		vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeBytes), &push);
		return;
	}

	std::array<VkDeviceSize, 3> offsets{};
	std::array<VkMarkingTypeARM, 3> markingTypes{};
	std::array<VkMarkingSubTypeARM, 3> subTypes{};
	for (uint32_t i = 0; i < addressCount; i++)
	{
		offsets[i] = sizeof(VkDeviceAddress) * i;
		markingTypes[i] = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
		subTypes[i].deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
	}

	VkMarkedOffsetsARM markings { VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr };
	markings.count = addressCount;
	markings.pOffsets = offsets.data();
	markings.pMarkingTypes = markingTypes.data();
	markings.pSubTypes = subTypes.data();

#ifdef VULKAN_1_4
	if (vulkan.apiVersion >= VK_API_VERSION_1_4)
	{
		VkPushConstantsInfo pushinfo = { VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO, nullptr };
		pushinfo.layout = layout;
		pushinfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushinfo.offset = 0;
		pushinfo.size = sizeBytes;
		pushinfo.pValues = &push;
		pushinfo.pNext = &markings;
		vkCmdPushConstants2(commandBuffer, &pushinfo);
	}
	else
#endif
	{
		VkPushConstantsInfoKHR pushinfo = { VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR, nullptr };
		pushinfo.layout = layout;
		pushinfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushinfo.offset = 0;
		pushinfo.size = sizeBytes;
		pushinfo.pValues = &push;
		pushinfo.pNext = &markings;
		vulkan.vkCmdPushConstants2(commandBuffer, &pushinfo);
	}
}

static void show_usage()
{
	usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return parseCmdopt(i, argc, argv, reqs);
}

using namespace tracetooltests;

class benchmarkContext : public GraphicContext
{
public:
	benchmarkContext() : GraphicContext() {}

	~benchmarkContext()
	{
		destroy();
	}

	void destroy()
	{
		DLOG3("MEM detection: vulkan_compute_bda_copying_address_minimal benchmark destroy().");

		m_colorBuffer = nullptr;
		m_addressBuffer = nullptr;
		m_interleaveBuffer = nullptr;
		m_outputBuffer = nullptr;

		m_interleavePipeline = nullptr;
		m_outputPipeline = nullptr;
		m_computePipelineLayout = nullptr;

		if (m_frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, m_frameFence, nullptr);
			m_frameFence = VK_NULL_HANDLE;
		}
	}

	std::unique_ptr<Buffer> m_colorBuffer;
	std::unique_ptr<Buffer> m_addressBuffer;
	std::unique_ptr<Buffer> m_interleaveBuffer;
	std::unique_ptr<Buffer> m_outputBuffer;

	std::unique_ptr<ComputePipeline> m_interleavePipeline;
	std::unique_ptr<ComputePipeline> m_outputPipeline;
	std::shared_ptr<PipelineLayout> m_computePipelineLayout;

	VkFence m_frameFence = VK_NULL_HANDLE;
};

static std::unique_ptr<benchmarkContext> p_benchmark = nullptr;

static void create_compute_pipeline(std::unique_ptr<ComputePipeline>& pipeline, unsigned char* spirv, unsigned int spirv_len,
	const std::vector<VkSpecializationMapEntry>& smentries, const std::vector<uint32_t>& sdata)
{
	std::vector<uint32_t> code = copy_shader(spirv, spirv_len);
	assert(shader_has_device_addresses(code));

	auto shader = std::make_shared<Shader>(p_benchmark->m_vulkanSetup.device);
	shader->create(spirv, spirv_len);

	ShaderPipelineState shaderStage(VK_SHADER_STAGE_COMPUTE_BIT, shader);
	shaderStage.setSpecialization(smentries, 4 * sdata.size(), const_cast<uint32_t*>(sdata.data()));

	pipeline = std::make_unique<ComputePipeline>(p_benchmark->m_vulkanSetup.device);
	pipeline->create(p_benchmark->m_computePipelineLayout->getHandle(), shaderStage);

	shaderStage.destroy();
}

static void populate_address_and_color_buffers()
{
	const uint32_t pixel_count = p_benchmark->width * p_benchmark->height;
	assert(pixel_count == 2);

	p_benchmark->m_colorBuffer->map();
	p_benchmark->m_addressBuffer->map();

	uint32_t* colorData = reinterpret_cast<uint32_t*>(p_benchmark->m_colorBuffer->m_mappedAddress);
	uint32_t* addressData = reinterpret_cast<uint32_t*>(p_benchmark->m_addressBuffer->m_mappedAddress);
	std::memset(colorData, 0, static_cast<size_t>(p_benchmark->m_colorBuffer->getSize()));
	std::memset(addressData, 0, static_cast<size_t>(p_benchmark->m_addressBuffer->getSize()));

	std::vector<VkDeviceSize> marked_offsets(pixel_count);
	for (uint32_t i = 0; i < pixel_count; i++)
	{
		const VkDeviceAddress address = p_benchmark->m_outputBuffer->getBufferDeviceAddress() + i * sizeof(VkDeviceAddress);

		colorData[i * 2] = static_cast<uint32_t>(address & 0xFFFFFFFF);
		colorData[i * 2 + 1] = static_cast<uint32_t>(address >> 32);
		addressData[i * 2] = static_cast<uint32_t>(address & 0xFFFFFFFF);
		addressData[i * 2 + 1] = static_cast<uint32_t>(address >> 32);
		marked_offsets[i] = i * sizeof(VkDeviceAddress);
	}

	testFlushMemoryDeviceAddresses(p_benchmark->m_vulkanSetup, p_benchmark->m_colorBuffer->getMemory(), 0,
		p_benchmark->m_colorBuffer->getSize(), marked_offsets, VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM, true);
	testFlushMemoryDeviceAddresses(p_benchmark->m_vulkanSetup, p_benchmark->m_addressBuffer->getMemory(), 0,
		p_benchmark->m_addressBuffer->getSize(), marked_offsets, VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM, true);

	p_benchmark->m_colorBuffer->unmap();
	p_benchmark->m_addressBuffer->unmap();
}

static void render(vulkan_setup_t& vulkan)
{
	const uint32_t groupCount_x = static_cast<uint32_t>(std::ceil(p_benchmark->width / static_cast<float>(p_benchmark->wg_size)));
	const uint32_t groupCount_y = static_cast<uint32_t>(std::ceil(p_benchmark->height / static_cast<float>(p_benchmark->wg_size)));

	benchmarking& bench = p_benchmark->m_vulkanSetup.bench;
	while (p__loops--)
	{
		VkCommandBuffer commandBuffer = p_benchmark->m_defaultCommandBuffer->getHandle();
		vkResetCommandBuffer(commandBuffer, 0);

		populate_address_and_color_buffers();

		p_benchmark->m_defaultCommandBuffer->begin();

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, p_benchmark->m_interleavePipeline->getHandle());
		pushAddress push_address{};
		push_address.BDA = p_benchmark->m_interleaveBuffer->getBufferDeviceAddress();
		push_address.colorBDA = p_benchmark->m_colorBuffer->getBufferDeviceAddress();
		push_address.addressBDA = p_benchmark->m_addressBuffer->getBufferDeviceAddress();
		push_address_constants(commandBuffer, p_benchmark->m_vulkanSetup, p_benchmark->m_computePipelineLayout->getHandle(),
			push_address, 3, sizeof(push_address));
		vkCmdDispatch(commandBuffer, groupCount_x, groupCount_y, 1);

		VkBufferMemoryBarrier buffer_barrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr };
		buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		buffer_barrier.offset = 0;
		buffer_barrier.size = VK_WHOLE_SIZE;
		buffer_barrier.buffer = p_benchmark->m_interleaveBuffer->getHandle();
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &buffer_barrier, 0, nullptr);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, p_benchmark->m_outputPipeline->getHandle());
		push_address = {};
		push_address.BDA = p_benchmark->m_interleaveBuffer->getBufferDeviceAddress();
		push_address_constants(commandBuffer, p_benchmark->m_vulkanSetup, p_benchmark->m_computePipelineLayout->getHandle(),
			push_address, 1, sizeof(VkDeviceAddress));
		vkCmdDispatch(commandBuffer, groupCount_x, groupCount_y, 1);

		p_benchmark->m_defaultCommandBuffer->end();

		bench_start_iteration(bench);
		p_benchmark->submit(p_benchmark->m_defaultQueue, std::vector<std::shared_ptr<CommandBuffer>> { p_benchmark->m_defaultCommandBuffer }, p_benchmark->m_frameFence);
		vkWaitForFences(vulkan.device, 1, &p_benchmark->m_frameFence, VK_TRUE, UINT64_MAX);
		vkResetFences(vulkan.device, 1, &p_benchmark->m_frameFence);
		bench_stop_iteration(bench);
	}
}

int main(int argc, char** argv)
{
	p__loops = 1;
	p_benchmark = std::make_unique<benchmarkContext>();

	vulkan_req_t req;
	req.options["width"] = 2;
	req.options["height"] = 1;
	req.options["wg_size"] = 1;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.bufferDeviceAddress = true;
	req.reqfeat12.bufferDeviceAddress = VK_TRUE;
	req.apiVersion = VK_API_VERSION_1_2;
	req.device_extensions.push_back("VK_KHR_maintenance6");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_bda_copying_address_minimal", req);

	p_benchmark->initBasic(vulkan, req);

	const uint32_t pixel_count = p_benchmark->width * p_benchmark->height;
	assert(pixel_count == 2);

	std::vector<VkPushConstantRange> range = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushAddress) } };
	auto pipeline_layout = std::make_shared<PipelineLayout>(vulkan.device);
	pipeline_layout->create(range);
	p_benchmark->m_computePipelineLayout = pipeline_layout;

	std::vector<VkSpecializationMapEntry> smentries(6);
	for (unsigned i = 0; i < smentries.size(); i++)
	{
		smentries[i].constantID = i;
		smentries[i].offset = i * 4;
		smentries[i].size = 4;
	}

	std::vector<uint32_t> sdata(6);
	sdata[0] = p_benchmark->wg_size;
	sdata[1] = p_benchmark->wg_size;
	sdata[2] = 1;
	sdata[3] = p_benchmark->width;
	sdata[4] = p_benchmark->height;
	sdata[5] = pixel_count;

	create_compute_pipeline(p_benchmark->m_interleavePipeline, vulkan_compute_bda_copying_address_interleave_spirv,
		vulkan_compute_bda_copying_address_interleave_spirv_len, smentries, sdata);
	create_compute_pipeline(p_benchmark->m_outputPipeline, vulkan_compute_bda_copying_address_output_spirv,
		vulkan_compute_bda_copying_address_output_spirv_len, smentries, sdata);
	pipeline_layout = nullptr;

	const VkDeviceSize address_buffer_size = pixel_count * sizeof(VkDeviceAddress);
	p_benchmark->m_outputBuffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->m_outputBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		address_buffer_size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	p_benchmark->m_colorBuffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->m_colorBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		address_buffer_size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	p_benchmark->m_addressBuffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->m_addressBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		address_buffer_size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	p_benchmark->m_interleaveBuffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->m_interleaveBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		pixel_count * sizeof(VkDeviceAddress) * 2, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	check(vkCreateFence(vulkan.device, &fenceInfo, nullptr, &p_benchmark->m_frameFence));

	render(vulkan);

	vkDeviceWaitIdle(vulkan.device);
	p_benchmark = nullptr;
	return 0;
}
