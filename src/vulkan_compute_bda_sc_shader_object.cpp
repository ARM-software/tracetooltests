// Compute test for buffer device addresses passed through shader-object specialization constants.

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// Reuse the compute_bda_sc shader. Its specialization constants are:
// 0..2 workgroup size, 3 width, 4 height, 5..6 output buffer device address.
#include "vulkan_compute_bda_sc.inc"

static bool multi_create_info = false;

static void show_usage()
{
	compute_usage();
	printf("--multi-create-info   Create two shader objects in a single vkCreateShadersEXT call\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], nullptr, "--multi-create-info"))
	{
		multi_create_info = true;
		return true;
	}
	return compute_cmdopt(i, argc, argv, reqs);
}

static std::vector<VkSpecializationMapEntry> create_specialization_map()
{
	std::vector<VkSpecializationMapEntry> entries(7);
	for (uint32_t i = 0; i < entries.size(); i++)
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
	data[5] = static_cast<int32_t>(address & 0xffffffffull);
	data[6] = static_cast<int32_t>(address >> 32);
	return data;
}

static VkShaderCreateInfoEXT create_shader_info(const std::vector<uint32_t>& code, const VkSpecializationInfo* specialization)
{
	VkShaderCreateInfoEXT info = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT, nullptr };
	info.flags = 0;
	info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.nextStage = 0;
	info.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
	info.codeSize = code.size() * sizeof(uint32_t);
	info.pCode = code.data();
	info.pName = "main";
	info.setLayoutCount = 0;
	info.pSetLayouts = nullptr;
	info.pushConstantRangeCount = 0;
	info.pPushConstantRanges = nullptr;
	info.pSpecializationInfo = specialization;
	return info;
}

static void add_trace_helper_marking(VkShaderCreateInfoEXT& shader_info, VkMarkedOffsetsARM& markings,
	VkMarkingTypeARM& marking_type, VkMarkingSubTypeARM& subtype, VkDeviceSize& offset)
{
	offset = 5 * sizeof(int32_t);
	marking_type = VK_MARKING_TYPE_DEVICE_ADDRESS_ARM;
	subtype = { .deviceAddressType = VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM };
	markings = { VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, shader_info.pNext };
	markings.count = 1;
	markings.pMarkingTypes = &marking_type;
	markings.pSubTypes = &subtype;
	markings.pOffsets = &offset;
	shader_info.pNext = &markings;
}

static std::vector<VkShaderEXT> create_shaders(vulkan_setup_t& vulkan, compute_resources& resources, vulkan_req_t& reqs, VkDeviceAddress address,
	PFN_vkCreateShadersEXT create_shaders_ext)
{
	assert(shader_has_device_addresses(resources.code));

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

	VkShaderCreateInfoEXT shader_infos[2] = {
		create_shader_info(resources.code, &specialization),
		create_shader_info(resources.code, &unused_specialization),
	};

	VkMarkedOffsetsARM markings = {};
	VkMarkingTypeARM marking_type = {};
	VkMarkingSubTypeARM subtype = {};
	VkDeviceSize marking_offset = 0;
	if (vulkan.has_trace_helpers)
	{
		add_trace_helper_marking(shader_infos[0], markings, marking_type, subtype, marking_offset);
	}

	const uint32_t shader_count = multi_create_info ? 2 : 1;
	std::vector<VkShaderEXT> shaders(shader_count, VK_NULL_HANDLE);
	VkResult result = create_shaders_ext(vulkan.device, shader_count, shader_infos, nullptr, shaders.data());
	check(result);
	return shaders;
}

int main(int argc, char** argv)
{
	p__loops = 1;
	VkPhysicalDeviceShaderObjectFeaturesEXT shader_object_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT, nullptr };
	shader_object_features.shaderObject = VK_TRUE;

	vulkan_req_t reqs;
	reqs.options["width"] = 64;
	reqs.options["height"] = 64;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.reqfeat13.dynamicRendering = VK_TRUE;
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
	reqs.device_extensions.push_back(VK_EXT_SHADER_OBJECT_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&shader_object_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_bda_sc_shader_object", reqs);
	compute_resources resources = compute_init(vulkan, reqs);
	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int workgroup_size = std::get<int>(reqs.options.at("wg_size"));

	MAKEDEVICEPROCADDR(vulkan, vkCreateShadersEXT);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyShaderEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBindShadersEXT);

	resources.code = copy_shader(vulkan_compute_bda_sc_spirv, vulkan_compute_bda_sc_spirv_len);

	VkBufferDeviceAddressInfo address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
	address_info.buffer = resources.buffer;
	const VkDeviceAddress address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
	std::vector<VkShaderEXT> shaders = create_shaders(vulkan, resources, reqs, address, pf_vkCreateShadersEXT);

	for (uint32_t frame = 0; frame < p__loops; frame++)
	{
		test_marker(vulkan, "Frame " + std::to_string(frame));

		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VkResult result = vkBeginCommandBuffer(resources.commandBuffer, &begin_info);
		check(result);
		VkShaderStageFlagBits stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pf_vkCmdBindShadersEXT(resources.commandBuffer, 1, &stage, shaders.data());
		vkCmdDispatch(resources.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
		result = vkEndCommandBuffer(resources.commandBuffer);
		check(result);

		compute_submit(vulkan, resources, reqs);
	}

	for (VkShaderEXT shader : shaders)
	{
		pf_vkDestroyShaderEXT(vulkan.device, shader, nullptr);
	}
	compute_done(vulkan, resources, reqs);
	test_done(vulkan);
}
