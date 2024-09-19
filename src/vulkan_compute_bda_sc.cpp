// Minimal compute unit test
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_bda_sc.comp -o vulkan_compute_bda_sc.spirv
//   xxd -i vulkan_compute_bda_sc.spirv > vulkan_compute_bda_sc.inc
#include "vulkan_compute_bda_sc.inc"

#include <cmath>

struct pixel
{
	float r, g, b, a;
};

static void show_usage()
{
	compute_usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return compute_cmdopt(i, argc, argv, reqs);
}

static void bda_sc_create_pipeline(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs, VkDeviceAddress address)
{
	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	createInfo.pCode = r.code.data();
	createInfo.codeSize = r.code.size();
	VkResult result = vkCreateShaderModule(vulkan.device, &createInfo, NULL, &r.computeShaderModule);
	check(result);
	assert(shader_has_buffer_devices_addresses(r.code.data(), r.code.size()));

	std::vector<VkSpecializationMapEntry> smentries(7);
	for (unsigned i = 0; i < smentries.size(); i++)
	{
		smentries[i].constantID = i;
		smentries[i].offset = i * 4;
		smentries[i].size = 4;
	}

	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int wg_size = std::get<int>(reqs.options.at("wg_size"));
	std::vector<int32_t> sdata(7);
	sdata[0] = wg_size; // workgroup x size
	sdata[1] = wg_size; // workgroup y size
	sdata[2] = 1; // workgroup z size
	sdata[3] = width; // surface width
	sdata[4] = height; // surface height
	sdata[5] = address & UINT32_MAX; // buffer device address of output buffer - low bits
	sdata[6] = address >> 32; // buffer device address of output buffer - high bits

	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = smentries.size();
	specInfo.pMapEntries = smentries.data();
	specInfo.dataSize = sdata.size() * 4;
	specInfo.pData = sdata.data();

	VkPipelineShaderStageCreateInfo shaderStageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStageCreateInfo.module = r.computeShaderModule;
	shaderStageCreateInfo.pName = "main";
	shaderStageCreateInfo.pSpecializationInfo = &specInfo;

	VkDeviceSize markup_location = 5;
	VkAddMemoryMarkupTRACETOOLTEST amm = { VK_STRUCTURE_TYPE_MEMORY_MARKUP_TRACETOOLTEST, shaderStageCreateInfo.pNext };
	amm.target = VK_MEMORY_MARKUP_TARGET_SPECIALIZATION_CONSTANTS_TRACETOOLTEST;
	amm.count = 1;
	amm.pMarkings = &markup_location;
	if (vulkan.memory_marking_supported) shaderStageCreateInfo.pNext = &amm;

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipelineLayoutCreateInfo.setLayoutCount = 0;
	pipelineLayoutCreateInfo.pSetLayouts = nullptr;
	result = vkCreatePipelineLayout(vulkan.device, &pipelineLayoutCreateInfo, NULL, &r.pipelineLayout);
	check(result);

        VkComputePipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
        pipelineCreateInfo.stage = shaderStageCreateInfo;
        pipelineCreateInfo.layout = r.pipelineLayout;

	VkPipelineCreationFeedback creationfeedback = { 0, 0 };
	VkPipelineCreationFeedbackCreateInfo feedinfo = { VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO, nullptr, &creationfeedback, 0, nullptr };
	if (reqs.apiVersion == VK_API_VERSION_1_3)
	{
		pipelineCreateInfo.pNext = &feedinfo;
	}

	if (reqs.options.count("pipelinecache"))
	{
		char* blob = nullptr;
		VkPipelineCacheCreateInfo cacheinfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr };
		cacheinfo.flags = 0;
		if (reqs.options.count("cachefile") && exists_blob(std::get<std::string>(reqs.options.at("cachefile"))))
		{
			ILOG("Reading pipeline cache data from %s", std::get<std::string>(reqs.options.at("cachefile")).c_str());
			uint32_t size = 0;
			blob = load_blob(std::get<std::string>(reqs.options.at("cachefile")), &size);
			cacheinfo.initialDataSize = size;
			cacheinfo.pInitialData = blob;
		}
		result = vkCreatePipelineCache(vulkan.device, &cacheinfo, nullptr, &r.cache);
		free(blob);
	}

	result = vkCreateComputePipelines(vulkan.device, r.cache, 1, &pipelineCreateInfo, nullptr, &r.pipeline);
	check(result);

	if (reqs.apiVersion == VK_API_VERSION_1_3)
	{
		if (creationfeedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT)
		{
			ILOG("VkPipelineCreationFeedback value = %lu ns", (unsigned long)creationfeedback.duration);
		}
		else
		{
			ILOG("VkPipelineCreationFeedback invalid");
		}
	}
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.options["width"] = 640;
	reqs.options["height"] = 480;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_bda_sc", reqs);
	compute_resources r = compute_init(vulkan, reqs);
	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int workgroup_size = std::get<int>(reqs.options.at("wg_size"));
	VkResult result;

	uint32_t code_size = long(ceil(vulkan_compute_bda_sc_spirv_len / 4.0)) * 4;
	r.code.resize(code_size);
	memcpy(r.code.data(), vulkan_compute_bda_sc_spirv, vulkan_compute_bda_sc_spirv_len);

	VkBufferDeviceAddressInfo bdainfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
	bdainfo.buffer = r.buffer;
	VkDeviceAddress address = vkGetBufferDeviceAddress(vulkan.device, &bdainfo);
	bda_sc_create_pipeline(vulkan, r, reqs, address);

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
	check(result);
	vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
	vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
	result = vkEndCommandBuffer(r.commandBuffer);
	check(result);

	compute_submit(vulkan, r, reqs);
	compute_done(vulkan, r, reqs);
	test_done(vulkan);
}
