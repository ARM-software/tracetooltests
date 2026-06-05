#include "vulkan_common.h"

#include <array>

// Reuse an existing compute shader; this test only needs pipeline creation.
#include "vulkan_compute_1.inc"

static void show_usage()
{
	printf("Minimal VK_ARM_pipeline_opacity_micromap pipeline flag test\n");
}

int main(int argc, char** argv)
{
	VkPhysicalDevicePipelineOpacityMicromapFeaturesARM pipeline_micromap_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_OPACITY_MICROMAP_FEATURES_ARM, nullptr
	};
	pipeline_micromap_features.pipelineOpacityMicromap = VK_TRUE;
	VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, &pipeline_micromap_features
	};
	maintenance5_features.maintenance5 = VK_TRUE;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &maintenance5_features
	};
	acceleration_features.accelerationStructure = VK_TRUE;

	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_ARM_PIPELINE_OPACITY_MICROMAP_EXTENSION_NAME);
	reqs.bufferDeviceAddress = true;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&acceleration_features);
	reqs.usage = show_usage;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_pipeline_opacity_micromap", reqs);

	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	layout_info.bindingCount = 1;
	layout_info.pBindings = &binding;

	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	check(vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &descriptor_set_layout));

	VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &descriptor_set_layout;

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	check(vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &pipeline_layout));

	const std::vector<uint32_t> code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);
	VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	module_info.codeSize = code.size() * sizeof(code[0]);
	module_info.pCode = code.data();

	VkShaderModule shader_module = VK_NULL_HANDLE;
	check(vkCreateShaderModule(vulkan.device, &module_info, nullptr, &shader_module));

	std::array<VkSpecializationMapEntry, 5> map_entries{};
	for (uint32_t i = 0; i < map_entries.size(); i++)
	{
		map_entries[i].constantID = i;
		map_entries[i].offset = i * sizeof(int32_t);
		map_entries[i].size = sizeof(int32_t);
	}

	const std::array<int32_t, 5> specialization_data = { 1, 1, 1, 1, 1 };
	VkSpecializationInfo specialization_info{};
	specialization_info.mapEntryCount = map_entries.size();
	specialization_info.pMapEntries = map_entries.data();
	specialization_info.dataSize = specialization_data.size() * sizeof(specialization_data[0]);
	specialization_info.pData = specialization_data.data();

	VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr};
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = shader_module;
	stage.pName = "main";
	stage.pSpecializationInfo = &specialization_info;

	VkPipelineCreateFlags2CreateInfo flags2{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO, nullptr};
	flags2.flags = VK_PIPELINE_CREATE_2_DISALLOW_OPACITY_MICROMAP_BIT_ARM;

	VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, &flags2};
	pipeline_info.stage = stage;
	pipeline_info.layout = pipeline_layout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	bench_start_iteration(vulkan.bench);
	check(vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));
	bench_stop_iteration(vulkan.bench);

	vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyShaderModule(vulkan.device, shader_module, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, descriptor_set_layout, nullptr);

	test_done(vulkan);
	return 0;
}
