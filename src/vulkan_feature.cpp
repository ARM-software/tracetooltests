#include "vulkan_utility.h"
#include "vulkan_feature_detect.h"
#include "vulkan_compute_bda_sc.inc"

#include <cmath>

#pragma GCC diagnostic ignored "-Wunused-variable"

feature_detection* feature_detection_instance = nullptr;

int main()
{
	feature_detection* f = vulkan_feature_detection_get();

	VkPhysicalDeviceFeatures feat10 = {};
	assert(feat10.logicOp == VK_FALSE); // not used
	VkPipelineColorBlendStateCreateInfo pipeinfo = {};
	struct_check_VkPipelineColorBlendStateCreateInfo(&pipeinfo);
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_FALSE); // still not used

	feat10.logicOp = VK_TRUE; // set to used, but not actually used
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_FALSE); // corretly corrected to not used

	feat10.logicOp = VK_TRUE; // set to used
	pipeinfo.logicOpEnable = VK_TRUE; // used here
	struct_check_VkPipelineColorBlendStateCreateInfo(&pipeinfo);
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_TRUE); // not changed, still used

	VkPhysicalDeviceVulkan12Features feat12 = {};
	auto adjusted = f->adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert(adjusted.size() == 0);
	assert(feat12.drawIndirectCount == VK_FALSE);
	assert(feat12.hostQueryReset == VK_FALSE);
	feat12.drawIndirectCount = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert(adjusted.size() == 1);
	assert(feat12.drawIndirectCount == VK_FALSE); // was adjusted
	feat12.drawIndirectCount = VK_TRUE;
	check_vkCmdDrawIndirectCount(0, 0, 0, 0, 0, 0, 0); // actually use feature
	f->adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert(feat12.drawIndirectCount == VK_TRUE); // now unchanged
	assert(feat12.hostQueryReset == VK_FALSE); // also unchanged

	VkBaseOutStructure second = { (VkStructureType)2, nullptr };
	VkBaseOutStructure first = { (VkStructureType)1, &second };
	VkBaseOutStructure root = { (VkStructureType)0, &first };
	assert(find_extension_parent(&root, (VkStructureType)0) == nullptr);
	assert(find_extension_parent(&root, (VkStructureType)1) == &root);
	assert(find_extension_parent(&root, (VkStructureType)2) == &first);
	assert(find_extension(&root, (VkStructureType)0) == &root);
	assert(find_extension(&root, (VkStructureType)1) == &first);
	assert(find_extension(&root, (VkStructureType)2) == &second);

	std::unordered_set<std::string> exts;
	exts.insert("VK_KHR_shader_atomic_int64");
	assert(exts.size() == 1);
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr };
	const char* extname = "VK_KHR_shader_atomic_int64";
	const char** namelist = { &extname };
	dci.ppEnabledExtensionNames = namelist;
	dci.enabledExtensionCount = 1;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == false);
	VkPhysicalDeviceShaderAtomicInt64Features pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_FALSE, VK_FALSE };
	dci.pNext = &pdsai64f;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == false);
	pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_TRUE, VK_TRUE };
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == true);
	adjusted = f->adjust_VkDeviceCreateInfo(&dci, exts);
	assert(adjusted.size() == 0);
	assert(dci.enabledExtensionCount == 1);
	assert(dci.pNext != nullptr);
	f->has_VkPhysicalDeviceShaderAtomicInt64Features.store(false);
	pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_FALSE, VK_FALSE };
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == false);
	std::unordered_set<std::string> removed = f->adjust_device_extensions(exts);
	assert(removed.size() == 1);
	assert(exts.size() == 0);
	adjusted = f->adjust_VkDeviceCreateInfo(&dci, exts);
	assert(adjusted.size() == 1);
	assert(adjusted.count("VK_KHR_shader_atomic_int64") == 1);
	assert(dci.pNext == nullptr);

	exts.insert("VK_KHR_map_memory2");
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_map_memory2 == false);
	removed = f->adjust_device_extensions(exts);
	assert(removed.size() == 1);
	assert(exts.size() == 0);

	exts.insert("VK_KHR_map_memory2");
	VkMemoryMapInfo map_info = { VK_STRUCTURE_TYPE_MEMORY_MAP_INFO, nullptr, 0, VK_NULL_HANDLE, 0, 0 };
	void* data = nullptr;
	check_vkMapMemory2KHR(VK_NULL_HANDLE, &map_info, &data);
	assert(f->has_VK_KHR_map_memory2 == true);
	removed = f->adjust_device_extensions(exts);
	assert(removed.size() == 0);
	assert(exts.size() == 1);

	f->has_VK_KHR_map_memory2.store(false);
	VkMemoryUnmapInfo unmap_info = { VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO, nullptr, 0, VK_NULL_HANDLE };
	check_vkUnmapMemory2(VK_NULL_HANDLE, &unmap_info);
	assert(f->has_VK_KHR_map_memory2 == true);

	std::unordered_set<std::string> robustness2_exts;
	robustness2_exts.insert("VK_KHR_robustness2");
	assert(robustness2_exts.size() == 1);
	assert(f->has_VK_KHR_robustness2 == false);
	removed = f->adjust_device_extensions(robustness2_exts);
	assert(removed.size() == 1);
	assert(robustness2_exts.size() == 0);

	robustness2_exts.insert("VK_KHR_robustness2");
	const char* robustness2_extname = "VK_KHR_robustness2";
	const char* robustness2_names[] = { robustness2_extname };
	VkPhysicalDeviceRobustness2FeaturesEXT robustness2_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, nullptr, VK_FALSE, VK_FALSE, VK_FALSE
	};
	dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &robustness2_features };
	dci.ppEnabledExtensionNames = robustness2_names;
	dci.enabledExtensionCount = 1;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_robustness2 == false);
	robustness2_features.nullDescriptor = VK_TRUE;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_robustness2 == true);
	removed = f->adjust_device_extensions(robustness2_exts);
	assert(removed.size() == 0);
	assert(robustness2_exts.size() == 1);

	f->has_VK_KHR_robustness2.store(false);
	removed = f->adjust_device_extensions(robustness2_exts);
	assert(removed.size() == 1);
	assert(robustness2_exts.size() == 0);
	adjusted = f->adjust_VkDeviceCreateInfo(&dci, robustness2_exts);
	assert(adjusted.size() == 1);
	assert(adjusted.count("VK_KHR_robustness2") == 1);
	assert(dci.pNext == nullptr);

	f->has_VK_EXT_robustness2.store(false);
	std::unordered_set<std::string> robustness2_ext_aliases;
	robustness2_ext_aliases.insert("VK_EXT_robustness2");
	assert(robustness2_ext_aliases.size() == 1);
	assert(f->has_VK_EXT_robustness2 == false);
	removed = f->adjust_device_extensions(robustness2_ext_aliases);
	assert(removed.size() == 1);
	assert(robustness2_ext_aliases.size() == 0);

	robustness2_ext_aliases.insert("VK_EXT_robustness2");
	const char* robustness2_ext_alias_name = "VK_EXT_robustness2";
	const char* robustness2_ext_alias_names[] = { robustness2_ext_alias_name };
	robustness2_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, nullptr, VK_TRUE, VK_FALSE, VK_FALSE };
	dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &robustness2_features };
	dci.ppEnabledExtensionNames = robustness2_ext_alias_names;
	dci.enabledExtensionCount = 1;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_EXT_robustness2 == true);
	assert(f->has_VK_KHR_robustness2 == true);
	removed = f->adjust_device_extensions(robustness2_ext_aliases);
	assert(removed.size() == 0);
	assert(robustness2_ext_aliases.size() == 1);
	adjusted = f->adjust_VkDeviceCreateInfo(&dci, robustness2_ext_aliases);
	assert(adjusted.size() == 0);
	assert(dci.pNext != nullptr);

	f->has_VK_EXT_robustness2.store(false);
	removed = f->adjust_device_extensions(robustness2_ext_aliases);
	assert(removed.size() == 1);
	assert(robustness2_ext_aliases.size() == 0);
	adjusted = f->adjust_VkDeviceCreateInfo(&dci, robustness2_ext_aliases);
	assert(adjusted.size() == 1);
	assert(adjusted.count("VK_EXT_robustness2") == 1);
	assert(dci.pNext == nullptr);

	std::unordered_set<std::string> robustness2_both_exts;
	robustness2_both_exts.insert("VK_KHR_robustness2");
	robustness2_both_exts.insert("VK_EXT_robustness2");
	const char* robustness2_both_names[] = { "VK_KHR_robustness2", "VK_EXT_robustness2" };
	robustness2_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, nullptr, VK_FALSE, VK_TRUE, VK_FALSE };
	dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &robustness2_features };
	dci.ppEnabledExtensionNames = robustness2_both_names;
	dci.enabledExtensionCount = 2;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_robustness2 == true);
	assert(f->has_VK_EXT_robustness2 == true);
	removed = f->adjust_device_extensions(robustness2_both_exts);
	assert(removed.size() == 0);
	assert(robustness2_both_exts.size() == 2);
	adjusted = f->adjust_VkDeviceCreateInfo(&dci, robustness2_both_exts);
	assert(adjusted.size() == 0);
	assert(dci.pNext != nullptr);

	f->has_VK_KHR_robustness2.store(false);
	f->has_VK_EXT_robustness2.store(false);
	removed = f->adjust_device_extensions(robustness2_both_exts);
	assert(removed.size() == 2);
	assert(robustness2_both_exts.size() == 0);
	adjusted = f->adjust_VkDeviceCreateInfo(&dci, robustness2_both_exts);
	assert(adjusted.size() == 2);
	assert(adjusted.count("VK_KHR_robustness2") == 1);
	assert(adjusted.count("VK_EXT_robustness2") == 1);
	assert(dci.pNext == nullptr);

	vulkan_feature_detection_reset();
	f = vulkan_feature_detection_get();

	std::unordered_set<std::string> multiview_exts;
	multiview_exts.insert("VK_KHR_multiview");
	assert(multiview_exts.size() == 1);
	removed = f->adjust_device_extensions(multiview_exts);
	assert(removed.size() == 1);
	assert(multiview_exts.size() == 0);

	multiview_exts.insert("VK_KHR_multiview");
	const char* multiview_extname = "VK_KHR_multiview";
	const char* multiview_names[] = { multiview_extname };
	VkPhysicalDeviceMultiviewFeatures multiview_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES, nullptr, VK_TRUE, VK_FALSE, VK_FALSE };
	dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &multiview_features };
	dci.ppEnabledExtensionNames = multiview_names;
	dci.enabledExtensionCount = 1;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == true);
	removed = f->adjust_device_extensions(multiview_exts);
	assert(removed.size() == 0);
	assert(multiview_exts.size() == 1);

	f->has_VK_KHR_multiview.store(false);
	removed = f->adjust_device_extensions(multiview_exts);
	assert(removed.size() == 1);
	assert(multiview_exts.size() == 0);
	adjusted = f->adjust_VkDeviceCreateInfo(&dci, multiview_exts);
	assert(adjusted.size() == 1);
	assert(adjusted.count("VK_KHR_multiview") == 1);
	assert(dci.pNext == nullptr);

	VkSubpassDependency dependency = {};
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	int32_t view_offset = 0;
	uint32_t view_mask = 0;
	uint32_t correlation_mask = 0;
	VkRenderPassMultiviewCreateInfo multiview_info = { VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO, nullptr, 1, &view_mask, 1, &view_offset, 1, &correlation_mask };
	VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, &multiview_info, 0, 0, nullptr, 1, &subpass, 1, &dependency };
	check_vkCreateRenderPass(VK_NULL_HANDLE, &rpci, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == false);
	dependency.dependencyFlags = VK_DEPENDENCY_VIEW_LOCAL_BIT;
	check_vkCreateRenderPass(VK_NULL_HANDLE, &rpci, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == true);

	vulkan_feature_detection_reset();
	f = vulkan_feature_detection_get();

	VkSubpassDescription2 subpass2 = { VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2, nullptr, 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr };
	VkSubpassDependency2 dependency2 = { VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2, nullptr, 0, 0, 0, 0, 0, 0, 0, 0 };
	VkRenderPassCreateInfo2 rpci2 = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2, nullptr, 0, 0, nullptr, 1, &subpass2, 1, &dependency2, 0, nullptr };
	check_vkCreateRenderPass2KHR(VK_NULL_HANDLE, &rpci2, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == false);
	subpass2.viewMask = 1;
	check_vkCreateRenderPass2(VK_NULL_HANDLE, &rpci2, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == true);

	vulkan_feature_detection_reset();
	f = vulkan_feature_detection_get();

	const uint32_t multiview_spirv[] = {
		SpvMagicNumber,
		0x00010000,
		0,
		3,
		0,
		(uint32_t(2) << 16) | SpvOpCapability,
		SpvCapabilityMultiView,
		(uint32_t(3) << 16) | SpvOpMemoryModel,
		SpvAddressingModelLogical,
		SpvMemoryModelGLSL450
	};
	multiview_exts.insert("VK_KHR_multiview");
	VkShaderModuleCreateInfo multiview_smci = {};
	multiview_smci.pCode = multiview_spirv;
	multiview_smci.codeSize = sizeof(multiview_spirv);
	VkResult multiview_result = check_vkCreateShaderModule(VK_NULL_HANDLE, &multiview_smci, nullptr, nullptr);
	assert(multiview_result == VK_SUCCESS);
	assert(f->has_VK_KHR_multiview == true);
	removed = f->adjust_device_extensions(multiview_exts);
	assert(removed.size() == 0);
	assert(multiview_exts.size() == 1);

	VkShaderModuleCreateInfo smci = {};
	smci.pCode = (uint32_t*)vulkan_compute_bda_sc_spirv;
	smci.codeSize = long(ceil(vulkan_compute_bda_sc_spirv_len / 4.0)) * sizeof(uint32_t);
	VkResult r = check_vkCreateShaderModule(VK_NULL_HANDLE, &smci, nullptr, nullptr);
	assert(r == VK_SUCCESS);

	return 0;
}
