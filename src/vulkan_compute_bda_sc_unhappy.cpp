// Compute test for lavatube specialization-constant marking edge cases.

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// Reuse the compute_bda_sc shader. Its specialization constants are:
// 0..2 workgroup size, 3 width, 4 height, 5..6 output buffer device address.
#include "vulkan_compute_bda_sc.inc"

static bool multi_create_info = false;
static bool stage_pnext = false;
static VkPhysicalDeviceMaintenance5Features maintenance5_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES, nullptr };

static void enable_maintenance5(vulkan_req_t& reqs)
{
	stage_pnext = true;
	reqs.device_extensions.push_back("VK_KHR_depth_stencil_resolve");
	reqs.device_extensions.push_back("VK_KHR_dynamic_rendering");
	reqs.device_extensions.push_back("VK_KHR_maintenance5");
	reqs.minApiVersion = std::max<unsigned>(VK_API_VERSION_1_2, reqs.minApiVersion);
	reqs.apiVersion = std::max<unsigned>(VK_API_VERSION_1_2, reqs.apiVersion);
	maintenance5_features.pNext = reqs.extension_features;
	maintenance5_features.maintenance5 = VK_TRUE;
	reqs.extension_features = (VkBaseInStructure*)&maintenance5_features;
}

static void show_usage()
{
	compute_usage();
	printf("--multi-create-info   Create two pipelines in a single vkCreateComputePipelines call\n");
	printf("--stage-pnext         Put VkShaderModuleCreateInfo in VkPipelineShaderStageCreateInfo::pNext\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], nullptr, "--multi-create-info"))
	{
		multi_create_info = true;
		return true;
	}
	if (match(argv[i], nullptr, "--stage-pnext"))
	{
		enable_maintenance5(reqs);
		return true;
	}
	return compute_cmdopt(i, argc, argv, reqs);
}

static std::vector<VkSpecializationMapEntry> create_specialization_map()
{
	std::vector<VkSpecializationMapEntry> entries(7);
	for (unsigned i = 0; i < entries.size(); i++)
	{
		entries[i].constantID = i;
		entries[i].offset = i * sizeof(int32_t);
		entries[i].size = sizeof(int32_t);
	}
	return entries;
}

static std::vector<int32_t> create_specialization_data(vulkan_req_t& reqs, VkDeviceAddress address)
{
	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int wg_size = std::get<int>(reqs.options.at("wg_size"));

	std::vector<int32_t> data(7);
	data[0] = wg_size;
	data[1] = wg_size;
	data[2] = 1;
	data[3] = width;
	data[4] = height;
	data[5] = address & UINT32_MAX;
	data[6] = address >> 32;
	return data;
}

static VkPipelineShaderStageCreateInfo create_stage_info(VkShaderModule module, const VkSpecializationInfo* specialization)
{
	VkPipelineShaderStageCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.module = module;
	info.pName = "main";
	info.pSpecializationInfo = specialization;
	return info;
}

static void add_trace_helper_marking(VkPipelineShaderStageCreateInfo& stage_info, VkMarkedOffsetsARM& markings,
	VkMarkingTypeARM& marking_type, VkMarkingSubTypeARM& subtype, VkDeviceSize& offset)
{
	offset = 5 * sizeof(int32_t);
	marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
	subtype = { .deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM };
	markings = { VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, stage_info.pNext };
	markings.count = 1;
	markings.pMarkingTypes = &marking_type;
	markings.pSubTypes = &subtype;
	markings.pOffsets = &offset;
	stage_info.pNext = &markings;
}

static std::vector<VkPipeline> create_pipelines(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs, VkDeviceAddress address)
{
	VkShaderModuleCreateInfo shader_module_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	shader_module_info.pCode = r.code.data();
	shader_module_info.codeSize = r.code.size() * sizeof(uint32_t);

	if (!stage_pnext)
	{
		VkResult result = vkCreateShaderModule(vulkan.device, &shader_module_info, nullptr, &r.computeShaderModule);
		check(result);
	}
	assert(shader_has_device_addresses(r.code));

	std::vector<VkSpecializationMapEntry> specialization_map = create_specialization_map();
	std::vector<int32_t> specialization_data = create_specialization_data(reqs, address);
	std::vector<int32_t> unused_specialization_data = create_specialization_data(reqs, address);

	VkSpecializationInfo specialization = {};
	specialization.mapEntryCount = specialization_map.size();
	specialization.pMapEntries = specialization_map.data();
	specialization.dataSize = specialization_data.size() * sizeof(int32_t);
	specialization.pData = specialization_data.data();

	VkSpecializationInfo unused_specialization = specialization;
	unused_specialization.pData = unused_specialization_data.data();

	VkPipelineShaderStageCreateInfo stages[2] = {
		create_stage_info(r.computeShaderModule, &specialization),
		create_stage_info(r.computeShaderModule, &unused_specialization),
	};

	if (stage_pnext)
	{
		stages[0].module = VK_NULL_HANDLE;
		stages[0].pNext = &shader_module_info;
		stages[1].module = VK_NULL_HANDLE;
		stages[1].pNext = &shader_module_info;
	}

	VkMarkedOffsetsARM markings = {};
	VkMarkingTypeARM marking_type = {};
	VkMarkingSubTypeARM subtype = {};
	VkDeviceSize marking_offset = 0;
	if (vulkan.has_trace_helpers)
	{
		add_trace_helper_marking(stages[0], markings, marking_type, subtype, marking_offset);
	}

	VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	VkResult result = vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &r.pipelineLayout);
	check(result);

	const uint32_t pipeline_count = multi_create_info ? 2 : 1;
	std::vector<VkComputePipelineCreateInfo> create_infos(pipeline_count);
	for (uint32_t i = 0; i < pipeline_count; i++)
	{
		create_infos[i] = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
		create_infos[i].stage = stages[i];
		create_infos[i].layout = r.pipelineLayout;
	}

	std::vector<VkPipeline> pipelines(pipeline_count, VK_NULL_HANDLE);
	result = vkCreateComputePipelines(vulkan.device, r.cache, pipeline_count, create_infos.data(), nullptr, pipelines.data());
	check(result);
	r.pipeline = pipelines[0];
	return pipelines;
}

int main(int argc, char** argv)
{
	p__loops = 1;
	vulkan_req_t reqs;
	reqs.options["width"] = 64;
	reqs.options["height"] = 64;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_bda_sc_unhappy", reqs);
	compute_resources r = compute_init(vulkan, reqs);
	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int workgroup_size = std::get<int>(reqs.options.at("wg_size"));

	r.code = copy_shader(vulkan_compute_bda_sc_spirv, vulkan_compute_bda_sc_spirv_len);

	VkBufferDeviceAddressInfo address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
	address_info.buffer = r.buffer;
	const VkDeviceAddress address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
	std::vector<VkPipeline> pipelines = create_pipelines(vulkan, r, reqs, address);

	for (unsigned frame = 0; frame < p__loops; frame++)
	{
		test_marker(vulkan, "Frame " + std::to_string(frame));

		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VkResult result = vkBeginCommandBuffer(r.commandBuffer, &begin_info);
		check(result);
		vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
		vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);

		compute_submit(vulkan, r, reqs);
	}

	for (size_t i = 1; i < pipelines.size(); i++)
	{
		vkDestroyPipeline(vulkan.device, pipelines[i], nullptr);
	}
	compute_done(vulkan, r, reqs);
	test_done(vulkan);
}
