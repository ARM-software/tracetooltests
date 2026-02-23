// Unit test to try out vulkan buffer device address

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"
#include <array>

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_bda_copying_address_interleave.comp -o vulkan_compute_bda_copying_address_interleave.spirv
//   xxd -i vulkan_compute_bda_copying_address_interleave.spirv > vulkan_compute_bda_copying_address_interleave.inc

#include "vulkan_compute_bda_copying_address_init.inc"
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
	printf("-gdriven/--gpu-driven             using gpu-driven to update.\n");
}

using namespace tracetooltests;

// ------------------------------ benchmark definition ------------------------
class benchmarkContext : public GraphicContext
{
public:
	benchmarkContext() : GraphicContext() {}
	~benchmarkContext() {
		destroy();
	}
	void destroy()
	{
		DLOG3("MEM detection: compute_bda_falseAddress benchmark destroy().");

		m_colorBuffer = nullptr;
		m_addressBuffer = nullptr;
		m_interleaveBuffer = nullptr;
		m_baseAddressBuffer = nullptr;
		m_outputBuffers.clear();
		m_outputBaseAddress.clear();

		m_initPipeline = nullptr;
		m_interleavePipeline = nullptr;
		m_outputPipeline = nullptr;

		if (m_frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, m_frameFence, nullptr);
			m_frameFence = VK_NULL_HANDLE;
		}
	}

	std::unique_ptr<Buffer> m_colorBuffer;
	std::unique_ptr<Buffer> m_addressBuffer;
	std::unique_ptr<Buffer> m_interleaveBuffer;
	std::vector<std::unique_ptr<Buffer>> m_outputBuffers;
	std::vector<VkDeviceAddress> m_outputBaseAddress;
	std::unique_ptr<Buffer> m_baseAddressBuffer;

	std::unique_ptr<ComputePipeline> m_initPipeline;
	std::unique_ptr<ComputePipeline> m_interleavePipeline;
	std::unique_ptr<ComputePipeline> m_outputPipeline;

	VkFence m_frameFence = VK_NULL_HANDLE;
	uint32_t numPixelsPerBuffer;
	bool gpu_driven = false;

};

static std::unique_ptr<benchmarkContext> p_benchmark = nullptr;
static void render(const vulkan_setup_t& vulkan);

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-gdriven", "--gpu-driven"))
	{
		p_benchmark->gpu_driven = true;
		return true;
	}

	return parseCmdopt(i, argc, argv, reqs);
}

#define NUM_OUTPUT_BUFFERS 4

int main(int argc, char** argv)
{
	p_benchmark = std::make_unique<benchmarkContext>();
	vulkan_req_t req;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.bufferDeviceAddress = true;
	req.reqfeat12.bufferDeviceAddress = VK_TRUE;
	req.apiVersion = VK_API_VERSION_1_2;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_bda_falseAddress", req);

	p_benchmark->initBasic(vulkan, req);

	p_benchmark->numPixelsPerBuffer = p_benchmark->width * p_benchmark->height / NUM_OUTPUT_BUFFERS;

	/******************* pipeline layout *********************/
	std::vector<VkPushConstantRange> range = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushAddress)} };
	auto pipeline_layout = std::make_shared<PipelineLayout>(vulkan.device);
	pipeline_layout->create(range);

	/******************* shader and pipeline *********************/
	std::vector<VkSpecializationMapEntry> smentries(6);
	for (unsigned i = 0; i < smentries.size(); i++)
	{
		smentries[i].constantID = i;
		smentries[i].offset = i * 4;
		smentries[i].size = 4;
	}

	std::vector<uint32_t> sdata(6);
	sdata[0] = p_benchmark->wg_size; // workgroup x size
	sdata[1] = p_benchmark->wg_size; // workgroup y size
	sdata[2] = 1; // workgroup z size
	sdata[3] = p_benchmark->width; // surface width
	sdata[4] = p_benchmark->height; // surface height
	sdata[5] = p_benchmark->numPixelsPerBuffer;

	if (p_benchmark->gpu_driven)
	{
		auto initShader = std::make_unique<Shader>(vulkan.device);
		initShader->create(vulkan_compute_bda_copying_address_init_spirv, vulkan_compute_bda_copying_address_init_spirv_len);

		ShaderPipelineState initShaderStage(VK_SHADER_STAGE_COMPUTE_BIT, std::move(initShader));
		initShaderStage.setSpecialization(smentries, 4 * sdata.size(), sdata.data());

		p_benchmark->m_initPipeline = std::make_unique<ComputePipeline>(pipeline_layout);
		p_benchmark->m_initPipeline->create(initShaderStage);

		initShaderStage.destroy();
	}

	auto interleaveShader = std::make_unique<Shader>(vulkan.device);
	interleaveShader->create(vulkan_compute_bda_copying_address_interleave_spirv, vulkan_compute_bda_copying_address_interleave_spirv_len);

	auto outputShader = std::make_unique<Shader>(vulkan.device);
	outputShader->create(vulkan_compute_bda_copying_address_output_spirv, vulkan_compute_bda_copying_address_output_spirv_len);

	ShaderPipelineState interleaveShaderStage(VK_SHADER_STAGE_COMPUTE_BIT, std::move(interleaveShader));
	interleaveShaderStage.setSpecialization(smentries, 4 * sdata.size(), sdata.data());

	ShaderPipelineState outputShaderStage(VK_SHADER_STAGE_COMPUTE_BIT, std::move(outputShader));
	outputShaderStage.setSpecialization(smentries, 4 * sdata.size(), sdata.data());

	p_benchmark->m_interleavePipeline = std::make_unique<ComputePipeline>(pipeline_layout);
	p_benchmark->m_interleavePipeline->create(interleaveShaderStage);

	p_benchmark->m_outputPipeline = std::make_unique<ComputePipeline>(pipeline_layout);
	p_benchmark->m_outputPipeline->create(outputShaderStage);

	outputShaderStage.destroy();
	interleaveShaderStage.destroy();
	pipeline_layout = nullptr;

	/******************* kinds of buffers *********************/
	VkDeviceSize size;
	// output buffers
	size = sizeof(VkDeviceAddress) * p_benchmark->numPixelsPerBuffer;
	for (int i =0 ; i < NUM_OUTPUT_BUFFERS; i++)
	{
		p_benchmark->m_outputBuffers.emplace_back(std::make_unique<Buffer>(vulkan));
		p_benchmark->m_outputBuffers[i]->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		p_benchmark->m_outputBaseAddress.push_back(p_benchmark->m_outputBuffers[i]->getBufferDeviceAddress());
	}

	if (p_benchmark->gpu_driven)
	{
		// buffer storing the gpu address of each output buffers
		size = NUM_OUTPUT_BUFFERS * sizeof(VkDeviceAddress);
		p_benchmark->m_baseAddressBuffer = std::make_unique<Buffer>(vulkan);
		p_benchmark->m_baseAddressBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	// color buffer
	VkMemoryPropertyFlags propFlags = 0;
	if (!p_benchmark->gpu_driven) propFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	else propFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	size = p_benchmark->width * p_benchmark->height * sizeof(VkDeviceAddress);
	p_benchmark->m_colorBuffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->m_colorBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, size, propFlags);

	// address buffer with same size
	p_benchmark->m_addressBuffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->m_addressBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, size, propFlags);

	// interleave buffer with double size
	size *= 2;
	p_benchmark->m_interleaveBuffer = std::make_unique<Buffer>(vulkan);
	p_benchmark->m_interleaveBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr};
	vkCreateFence(vulkan.device, &fenceInfo, nullptr, &p_benchmark->m_frameFence);

	/********************************** rendering ***********************************/
	render(vulkan);

	vkDeviceWaitIdle(vulkan.device);

	p_benchmark = nullptr;

	return 0;
}

static void populate_addressAndColorBuffer(uint32_t numPixelsPerBuffer)
{
	uint32_t numPixels = p_benchmark->width * p_benchmark->height;

	p_benchmark->m_colorBuffer->map();
	p_benchmark->m_addressBuffer->map();

	uint32_t *colorData = reinterpret_cast<uint32_t *>(p_benchmark->m_colorBuffer->m_mappedAddress);
	uint32_t *addressData = reinterpret_cast<uint32_t *>(p_benchmark->m_addressBuffer->m_mappedAddress);

	for (uint32_t i = 0; i < numPixels; i++)
	{
		uint32_t outputIndex = i / numPixelsPerBuffer;
		uint32_t outputOffset = i % numPixelsPerBuffer;
		uint64_t address = p_benchmark->m_outputBaseAddress[outputIndex] + outputOffset * sizeof(VkDeviceAddress);

		// uvec2(low,high)
		colorData[i*2] = static_cast<uint32_t>(address & 0xFFFFFFFF);
		colorData[i*2+1] = static_cast<uint32_t>(address >> 32);

		addressData[i*2] = static_cast<uint32_t>(address & 0xFFFFFFFF);
		addressData[i*2+1] = static_cast<uint32_t>(address >> 32);
	}

	p_benchmark->m_colorBuffer->unmap();
	p_benchmark->m_addressBuffer->unmap();

}

static void populate_addressAndColorBuffer_comp(VkCommandBuffer commandBuffer, uint32_t groupCount_x, uint32_t groupCount_y)
{
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, p_benchmark->m_initPipeline->getHandle());

	pushAddress push_address{};
	push_address.BDA = p_benchmark->m_baseAddressBuffer->getBufferDeviceAddress();
	push_address.colorBDA = p_benchmark->m_colorBuffer->getBufferDeviceAddress();
	push_address.addressBDA = p_benchmark->m_addressBuffer->getBufferDeviceAddress();
	push_address_constants(commandBuffer, p_benchmark->m_vulkanSetup, p_benchmark->m_initPipeline->m_pipelineLayout->getHandle(),
	                       push_address, 3, sizeof(push_address));

	vkCmdDispatch(commandBuffer, groupCount_x, groupCount_y, 1);

	// barrie color/address buffer: shader write -> read
	VkBufferMemoryBarrier buffer_barrie {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
	buffer_barrie.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	buffer_barrie.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	buffer_barrie.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrie.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrie.offset = 0;
	buffer_barrie.size = VK_WHOLE_SIZE;

	std::vector<VkBufferMemoryBarrier> buffer_barries {2, buffer_barrie};
	buffer_barries[0].buffer = p_benchmark->m_colorBuffer->getHandle();
	buffer_barries[1].buffer = p_benchmark->m_addressBuffer->getHandle();

	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, buffer_barries.data(), 0, nullptr);
}

static void render(const vulkan_setup_t& vulkan)
{
	uint32_t groupCount_x = (uint32_t)ceil(p_benchmark->width/float(p_benchmark->wg_size));
	uint32_t groupCount_y = (uint32_t)ceil(p_benchmark->height/float(p_benchmark->wg_size));

	benchmarking bench = vulkan.bench;

	while (p__loops--)
	{
		VkCommandBuffer defaultCmd = p_benchmark->m_defaultCommandBuffer->getHandle();
		vkResetCommandBuffer(defaultCmd, 0);

		p_benchmark->m_defaultCommandBuffer->begin();

		// populate output buffers addresses to color/address buffer
		if (p_benchmark->gpu_driven)
		{
			if (p_benchmark->m_vulkanSetup.has_trace_helpers)
			{
				std::array<VkDeviceSize, NUM_OUTPUT_BUFFERS> offsets{};
				std::array<VkMarkingTypeARM, NUM_OUTPUT_BUFFERS> markingTypes{};
				std::array<VkMarkingSubTypeARM, NUM_OUTPUT_BUFFERS> subTypes{};
				for (uint32_t i = 0; i < NUM_OUTPUT_BUFFERS; i++)
				{
					offsets[i] = i * sizeof(VkDeviceAddress);
					markingTypes[i] = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
					subTypes[i].deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM;
				}
				VkMarkedOffsetsARM markings { VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, nullptr };
				markings.count = NUM_OUTPUT_BUFFERS;
				markings.pOffsets = offsets.data();
				markings.pMarkingTypes = markingTypes.data();
				markings.pSubTypes = subTypes.data();

				VkUpdateBufferInfoARM updateInfo { VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, &markings };
				updateInfo.dstBuffer = p_benchmark->m_baseAddressBuffer->getHandle();
				updateInfo.dstOffset = 0;
				updateInfo.dataSize = p_benchmark->m_outputBuffers.size() * sizeof(VkDeviceAddress);
				updateInfo.pData = p_benchmark->m_outputBaseAddress.data();
				p_benchmark->m_vulkanSetup.vkCmdUpdateBuffer2(defaultCmd, &updateInfo);
			}
			else
			{
				vkCmdUpdateBuffer(defaultCmd, p_benchmark->m_baseAddressBuffer->getHandle(), 0, p_benchmark->m_outputBuffers.size() * sizeof(VkDeviceAddress), p_benchmark->m_outputBaseAddress.data());
			}
			VkMemoryBarrier memory_barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr};
			memory_barrier.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
			memory_barrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(defaultCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memory_barrier, 0, nullptr, 0, nullptr);

			populate_addressAndColorBuffer_comp(defaultCmd, groupCount_x, groupCount_y);
		}
		else
		{
			populate_addressAndColorBuffer(p_benchmark->numPixelsPerBuffer);
		}

		// interleave color and address into interleave buffer
		vkCmdBindPipeline(defaultCmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_benchmark->m_interleavePipeline->getHandle());
		pushAddress push_address{};
		push_address.BDA = p_benchmark->m_interleaveBuffer->getBufferDeviceAddress();
		push_address.colorBDA = p_benchmark->m_colorBuffer->getBufferDeviceAddress();
		push_address.addressBDA = p_benchmark->m_addressBuffer->getBufferDeviceAddress();
		push_address_constants(defaultCmd, p_benchmark->m_vulkanSetup, p_benchmark->m_interleavePipeline->m_pipelineLayout->getHandle(),
		                       push_address, 3, sizeof(push_address));

		vkCmdDispatch(defaultCmd, groupCount_x, groupCount_y, 1);
		// barrie interleave buffer: shader write -> read
		VkBufferMemoryBarrier buffer_barrie {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
		buffer_barrie.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		buffer_barrie.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		buffer_barrie.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		buffer_barrie.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		buffer_barrie.offset = 0;
		buffer_barrie.size = VK_WHOLE_SIZE;
		buffer_barrie.buffer = p_benchmark->m_interleaveBuffer->getHandle();
		vkCmdPipelineBarrier(defaultCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &buffer_barrie, 0, nullptr);

		// read out the color/address value from interleave, write color value into the address
		vkCmdBindPipeline(defaultCmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_benchmark->m_outputPipeline->getHandle());

		push_address.colorBDA = 0;
		push_address.addressBDA = 0;
		push_address.BDA = p_benchmark->m_interleaveBuffer->getBufferDeviceAddress();
		push_address_constants(defaultCmd, p_benchmark->m_vulkanSetup, p_benchmark->m_outputPipeline->m_pipelineLayout->getHandle(),
		                       push_address, 1, sizeof(VkDeviceAddress));
		vkCmdDispatch(defaultCmd, groupCount_x, groupCount_y, 1);

		p_benchmark->m_defaultCommandBuffer->end();

		bench_start_iteration(bench);

		// submit
		p_benchmark->submit(p_benchmark->m_defaultQueue, std::vector<std::shared_ptr<CommandBuffer>> {p_benchmark->m_defaultCommandBuffer}, p_benchmark->m_frameFence);

		vkWaitForFences(vulkan.device, 1, &p_benchmark->m_frameFence, VK_TRUE, UINT64_MAX);
		vkResetFences(vulkan.device, 1, &p_benchmark->m_frameFence);

		bench_stop_iteration(bench);
	}
}
