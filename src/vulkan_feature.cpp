#include "vulkan_utility.h"
#include "vulkan_feature_detect.h"
#include "vulkan_compute_bda_sc.inc"

#include <cmath>

#pragma GCC diagnostic ignored "-Wunused-variable"

int main()
{
	feature_detection detect;

	VkPhysicalDeviceFeatures feat10 = {};
	assert(feat10.logicOp == VK_FALSE); // not used
	VkPipelineColorBlendStateCreateInfo pipeinfo = {};
	detect.check_VkPipelineColorBlendStateCreateInfo(&pipeinfo);
	detect.adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_FALSE); // still not used

	feat10.logicOp = VK_TRUE; // set to used, but not actually used
	detect.adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_FALSE); // corretly corrected to not used

	feat10.logicOp = VK_TRUE; // set to used
	pipeinfo.logicOpEnable = VK_TRUE; // used here
	detect.check_VkPipelineColorBlendStateCreateInfo(&pipeinfo);
	detect.adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_TRUE); // not changed, still used

	VkPhysicalDeviceVulkan12Features feat12 = {};
	detect.adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert(feat12.drawIndirectCount == VK_FALSE);
	assert(feat12.hostQueryReset == VK_FALSE);
	feat12.drawIndirectCount = VK_TRUE;
	detect.adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert(feat12.drawIndirectCount == VK_FALSE); // was adjusted
	feat12.drawIndirectCount = VK_TRUE;
	detect.check_vkCmdDrawIndirectCount(0, 0, 0, 0, 0, 0, 0); // actually use feature
	detect.adjust_VkPhysicalDeviceVulkan12Features(feat12);
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
	detect.check_VkDeviceCreateInfo(&dci);
	assert(detect.has_VkPhysicalDeviceShaderAtomicInt64Features == false);
	VkPhysicalDeviceShaderAtomicInt64Features pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_FALSE, VK_FALSE };
	dci.pNext = &pdsai64f;
	detect.check_VkDeviceCreateInfo(&dci);
	assert(detect.has_VkPhysicalDeviceShaderAtomicInt64Features == false);
	pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_TRUE, VK_TRUE };
	detect.check_VkDeviceCreateInfo(&dci);
	assert(detect.has_VkPhysicalDeviceShaderAtomicInt64Features == true);
	detect.adjust_VkDeviceCreateInfo(&dci, exts);
	assert(dci.enabledExtensionCount == 1);
	assert(dci.pNext != nullptr);
	detect.has_VkPhysicalDeviceShaderAtomicInt64Features.store(false);
	pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_FALSE, VK_FALSE };
	detect.check_VkDeviceCreateInfo(&dci);
	assert(detect.has_VkPhysicalDeviceShaderAtomicInt64Features == false);
	std::unordered_set<std::string> removed = detect.adjust_device_extensions(exts);
	assert(removed.size() == 1);
	assert(exts.size() == 0);
	detect.adjust_VkDeviceCreateInfo(&dci, exts);
	assert(dci.pNext == nullptr);

	detect.parse_SPIRV((uint32_t*)vulkan_compute_bda_sc_spirv, long(ceil(vulkan_compute_bda_sc_spirv_len / 4.0)) * 4);

	return 0;
}
