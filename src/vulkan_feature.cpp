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
	for (auto s : adjusted) printf("Adjusted %s\n", s.c_str());
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
	f->adjust_VkDeviceCreateInfo(&dci, exts);
	assert(dci.enabledExtensionCount == 1);
	assert(dci.pNext != nullptr);
	f->has_VkPhysicalDeviceShaderAtomicInt64Features.store(false);
	pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_FALSE, VK_FALSE };
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == false);
	std::unordered_set<std::string> removed = f->adjust_device_extensions(exts);
	assert(removed.size() == 1);
	assert(exts.size() == 0);
	f->adjust_VkDeviceCreateInfo(&dci, exts);
	assert(dci.pNext == nullptr);

	VkShaderModuleCreateInfo smci = {};
	smci.pCode = (uint32_t*)vulkan_compute_bda_sc_spirv;
	smci.codeSize = long(ceil(vulkan_compute_bda_sc_spirv_len / 4.0)) * sizeof(uint32_t);
	VkResult r = check_vkCreateShaderModule(VK_NULL_HANDLE, &smci, nullptr, nullptr);
	assert(r == VK_SUCCESS);

	return 0;
}
