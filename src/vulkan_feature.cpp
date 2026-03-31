#include "vulkan_utility.h"
#include "src/usagetracker/vulkan_feature_detect.h"
#include "vulkan_compute_bda_sc.inc"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <unordered_set>

#pragma GCC diagnostic ignored "-Wunused-variable"

feature_detection* feature_detection_instance = nullptr;

static feature_detection* reset_detection()
{
	vulkan_feature_detection_reset();
	return vulkan_feature_detection_get();
}

static void assert_string_set_equals(const std::unordered_set<std::string>& actual, std::initializer_list<const char*> expected)
{
	assert(actual.size() == expected.size());
	for (const char* name : expected)
	{
		assert(actual.count(name) == 1);
	}
}

static void assert_removed_device_extensions(feature_detection* f, std::unordered_set<std::string>& exts, std::initializer_list<const char*> expected_removed)
{
	auto removed = f->adjust_device_extensions(exts);
	assert_string_set_equals(removed, expected_removed);
}

static void assert_adjusted_device_create_info(feature_detection* f, VkDeviceCreateInfo& dci, const std::unordered_set<std::string>& enabled_exts,
                                               std::initializer_list<const char*> expected_adjusted, bool expect_pnext)
{
	auto adjusted = f->adjust_VkDeviceCreateInfo(&dci, enabled_exts);
	assert_string_set_equals(adjusted, expected_adjusted);
	assert((dci.pNext != nullptr) == expect_pnext);
}

static void check_shader_module_code(const uint32_t* code, size_t code_size, uintptr_t handle_value)
{
	VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, code_size, code };
	VkShaderModule module = (VkShaderModule)handle_value;
	VkResult result = check_vkCreateShaderModule(VK_NULL_HANDLE, &smci, nullptr, &module);
	assert(result == VK_SUCCESS);
}

static void test_logic_op_adjustment()
{
	feature_detection* f = reset_detection();

	VkPhysicalDeviceFeatures feat10 = {};
	assert(feat10.logicOp == VK_FALSE);

	VkPipelineColorBlendStateCreateInfo pipeinfo = {};
	struct_check_VkPipelineColorBlendStateCreateInfo(&pipeinfo);
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_FALSE);

	feat10.logicOp = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_FALSE);

	feat10.logicOp = VK_TRUE;
	pipeinfo.logicOpEnable = VK_TRUE;
	struct_check_VkPipelineColorBlendStateCreateInfo(&pipeinfo);
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.logicOp == VK_TRUE);
}

static void test_texture_compression_adjustment()
{
	feature_detection* f = reset_detection();

	VkPhysicalDeviceFeatures feat10 = {};
	feat10.textureCompressionBC = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.textureCompressionBC == VK_FALSE);

	VkImageCreateInfo image_info = {
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0,
		VK_IMAGE_TYPE_2D, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, { 4, 4, 1 },
		1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr,
		VK_IMAGE_LAYOUT_UNDEFINED
	};
	check_vkCreateImage(VK_NULL_HANDLE, &image_info, nullptr, nullptr);
	feat10.textureCompressionBC = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.textureCompressionBC == VK_TRUE);

	f = reset_detection();

	VkImageSubresourceRange subresource_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageViewCreateInfo view_info = {
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, VK_NULL_HANDLE,
		VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, {},
		subresource_range
	};
	check_vkCreateImageView(VK_NULL_HANDLE, &view_info, nullptr, nullptr);
	feat10 = {};
	feat10.textureCompressionBC = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.textureCompressionBC == VK_TRUE);

	f = reset_detection();

	feat10 = {};
	feat10.textureCompressionETC2 = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.textureCompressionETC2 == VK_FALSE);

	image_info.format = VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
	check_vkCreateImage(VK_NULL_HANDLE, &image_info, nullptr, nullptr);
	feat10.textureCompressionETC2 = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.textureCompressionETC2 == VK_TRUE);

	f = reset_detection();

	feat10 = {};
	feat10.textureCompressionASTC_LDR = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.textureCompressionASTC_LDR == VK_FALSE);

	view_info.format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
	check_vkCreateImageView(VK_NULL_HANDLE, &view_info, nullptr, nullptr);
	feat10.textureCompressionASTC_LDR = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.textureCompressionASTC_LDR == VK_TRUE);
}

static void test_vulkan12_adjustment()
{
	feature_detection* f = reset_detection();

	VkPhysicalDeviceVulkan12Features feat12 = {};
	auto adjusted = f->adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert(adjusted.empty());
	assert(feat12.drawIndirectCount == VK_FALSE);
	assert(feat12.hostQueryReset == VK_FALSE);

	feat12.drawIndirectCount = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert_string_set_equals(adjusted, { "drawIndirectCount" });
	assert(feat12.drawIndirectCount == VK_FALSE);

	feat12.drawIndirectCount = VK_TRUE;
	check_vkCmdDrawIndirectCount(0, 0, 0, 0, 0, 0, 0);
	f->adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert(feat12.drawIndirectCount == VK_TRUE);
	assert(feat12.hostQueryReset == VK_FALSE);
}

static void test_extension_chain_helpers()
{
	VkBaseOutStructure second = { (VkStructureType)2, nullptr };
	VkBaseOutStructure first = { (VkStructureType)1, &second };
	VkBaseOutStructure root = { (VkStructureType)0, &first };

	assert(find_extension_parent(&root, (VkStructureType)0) == nullptr);
	assert(find_extension_parent(&root, (VkStructureType)1) == &root);
	assert(find_extension_parent(&root, (VkStructureType)2) == &first);
	assert(find_extension(&root, (VkStructureType)0) == &root);
	assert(find_extension(&root, (VkStructureType)1) == &first);
	assert(find_extension(&root, (VkStructureType)2) == &second);
}

static void test_shader_atomic_int64_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_shader_atomic_int64" };
	assert(exts.size() == 1);

	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr };
	const char* extname = "VK_KHR_shader_atomic_int64";
	const char* namelist[] = { extname };
	dci.ppEnabledExtensionNames = namelist;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == false);

	VkPhysicalDeviceShaderAtomicInt64Features pdsai64f = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_FALSE, VK_FALSE
	};
	dci.pNext = &pdsai64f;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == false);

	pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_TRUE, VK_TRUE };
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == true);
	assert_adjusted_device_create_info(f, dci, exts, {}, true);
	assert(dci.enabledExtensionCount == 1);

	f->has_VkPhysicalDeviceShaderAtomicInt64Features.store(false);
	pdsai64f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, nullptr, VK_FALSE, VK_FALSE };
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VkPhysicalDeviceShaderAtomicInt64Features == false);

	assert_removed_device_extensions(f, exts, { "VK_KHR_shader_atomic_int64" });
	assert(exts.empty());
	assert_adjusted_device_create_info(f, dci, exts, { "VK_KHR_shader_atomic_int64" }, false);
}

static void test_map_memory2_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_map_memory2" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_map_memory2 == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_map_memory2" });
	assert(exts.empty());

	exts.insert("VK_KHR_map_memory2");
	VkMemoryMapInfo map_info = { VK_STRUCTURE_TYPE_MEMORY_MAP_INFO, nullptr, 0, VK_NULL_HANDLE, 0, 0 };
	void* data = nullptr;
	check_vkMapMemory2KHR(VK_NULL_HANDLE, &map_info, &data);
	assert(f->has_VK_KHR_map_memory2 == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f->has_VK_KHR_map_memory2.store(false);
	VkMemoryUnmapInfo unmap_info = { VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO, nullptr, 0, VK_NULL_HANDLE };
	check_vkUnmapMemory2(VK_NULL_HANDLE, &unmap_info);
	assert(f->has_VK_KHR_map_memory2 == true);
}

static void test_robustness2_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> robustness2_exts = { "VK_KHR_robustness2" };
	assert(robustness2_exts.size() == 1);
	assert(f->has_VK_KHR_robustness2 == false);
	assert_removed_device_extensions(f, robustness2_exts, { "VK_KHR_robustness2" });
	assert(robustness2_exts.empty());

	robustness2_exts.insert("VK_KHR_robustness2");
	const char* robustness2_extname = "VK_KHR_robustness2";
	const char* robustness2_names[] = { robustness2_extname };
	VkPhysicalDeviceRobustness2FeaturesEXT robustness2_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, nullptr, VK_FALSE, VK_FALSE, VK_FALSE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &robustness2_features };
	dci.ppEnabledExtensionNames = robustness2_names;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_robustness2 == false);
	robustness2_features.nullDescriptor = VK_TRUE;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_robustness2 == true);
	assert_removed_device_extensions(f, robustness2_exts, {});
	assert(robustness2_exts.size() == 1);

	f->has_VK_KHR_robustness2.store(false);
	assert_removed_device_extensions(f, robustness2_exts, { "VK_KHR_robustness2" });
	assert(robustness2_exts.empty());
	assert_adjusted_device_create_info(f, dci, robustness2_exts, { "VK_KHR_robustness2" }, false);

	f->has_VK_EXT_robustness2.store(false);
	std::unordered_set<std::string> robustness2_ext_aliases = { "VK_EXT_robustness2" };
	assert(robustness2_ext_aliases.size() == 1);
	assert(f->has_VK_EXT_robustness2 == false);
	assert_removed_device_extensions(f, robustness2_ext_aliases, { "VK_EXT_robustness2" });
	assert(robustness2_ext_aliases.empty());

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
	assert_removed_device_extensions(f, robustness2_ext_aliases, {});
	assert(robustness2_ext_aliases.size() == 1);
	assert_adjusted_device_create_info(f, dci, robustness2_ext_aliases, {}, true);

	f->has_VK_EXT_robustness2.store(false);
	assert_removed_device_extensions(f, robustness2_ext_aliases, { "VK_EXT_robustness2" });
	assert(robustness2_ext_aliases.empty());
	assert_adjusted_device_create_info(f, dci, robustness2_ext_aliases, { "VK_EXT_robustness2" }, false);

	std::unordered_set<std::string> robustness2_both_exts = { "VK_KHR_robustness2", "VK_EXT_robustness2" };
	const char* robustness2_both_names[] = { "VK_KHR_robustness2", "VK_EXT_robustness2" };
	robustness2_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, nullptr, VK_FALSE, VK_TRUE, VK_FALSE };
	dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &robustness2_features };
	dci.ppEnabledExtensionNames = robustness2_both_names;
	dci.enabledExtensionCount = 2;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_robustness2 == true);
	assert(f->has_VK_EXT_robustness2 == true);
	assert_removed_device_extensions(f, robustness2_both_exts, {});
	assert(robustness2_both_exts.size() == 2);
	assert_adjusted_device_create_info(f, dci, robustness2_both_exts, {}, true);

	f->has_VK_KHR_robustness2.store(false);
	f->has_VK_EXT_robustness2.store(false);
	assert_removed_device_extensions(f, robustness2_both_exts, { "VK_KHR_robustness2", "VK_EXT_robustness2" });
	assert(robustness2_both_exts.empty());
	assert_adjusted_device_create_info(f, dci, robustness2_both_exts, { "VK_KHR_robustness2", "VK_EXT_robustness2" }, false);
}

static void test_multiview_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> multiview_exts = { "VK_KHR_multiview" };
	assert(multiview_exts.size() == 1);
	assert_removed_device_extensions(f, multiview_exts, { "VK_KHR_multiview" });
	assert(multiview_exts.empty());

	multiview_exts.insert("VK_KHR_multiview");
	const char* multiview_extname = "VK_KHR_multiview";
	const char* multiview_names[] = { multiview_extname };
	VkPhysicalDeviceMultiviewFeatures multiview_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES, nullptr, VK_TRUE, VK_FALSE, VK_FALSE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &multiview_features };
	dci.ppEnabledExtensionNames = multiview_names;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == true);
	assert_removed_device_extensions(f, multiview_exts, {});
	assert(multiview_exts.size() == 1);

	f->has_VK_KHR_multiview.store(false);
	assert_removed_device_extensions(f, multiview_exts, { "VK_KHR_multiview" });
	assert(multiview_exts.empty());
	assert_adjusted_device_create_info(f, dci, multiview_exts, { "VK_KHR_multiview" }, false);
}

static void test_multiview_render_pass_detection()
{
	feature_detection* f = reset_detection();

	VkSubpassDependency dependency = {};
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	int32_t view_offset = 0;
	uint32_t view_mask = 0;
	uint32_t correlation_mask = 0;
	VkRenderPassMultiviewCreateInfo multiview_info = {
		VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO, nullptr, 1, &view_mask, 1, &view_offset, 1, &correlation_mask
	};
	VkRenderPassCreateInfo rpci = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, &multiview_info, 0, 0, nullptr, 1, &subpass, 1, &dependency
	};

	check_vkCreateRenderPass(VK_NULL_HANDLE, &rpci, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == false);
	assert(f->core11.multiview == false);

	dependency.dependencyFlags = VK_DEPENDENCY_VIEW_LOCAL_BIT;
	check_vkCreateRenderPass(VK_NULL_HANDLE, &rpci, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == true);
	assert(f->core11.multiview == true);
}

static void test_multiview_render_pass2_detection()
{
	feature_detection* f = reset_detection();

	VkSubpassDescription2 subpass2 = {
		VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2, nullptr, 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr
	};
	VkSubpassDependency2 dependency2 = { VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2, nullptr, 0, 0, 0, 0, 0, 0, 0, 0 };
	VkRenderPassCreateInfo2 rpci2 = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2, nullptr, 0, 0, nullptr, 1, &subpass2, 1, &dependency2, 0, nullptr
	};

	check_vkCreateRenderPass2KHR(VK_NULL_HANDLE, &rpci2, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == false);
	assert(f->core11.multiview == false);

	subpass2.viewMask = 1;
	check_vkCreateRenderPass2(VK_NULL_HANDLE, &rpci2, nullptr, nullptr);
	assert(f->has_VK_KHR_multiview == true);
	assert(f->core11.multiview == true);
}

static void test_multiview_dynamic_rendering_detection()
{
	feature_detection* f = reset_detection();

	VkRenderingInfo rendering_info = {
		VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {}, 1, 0, 0, nullptr, nullptr, nullptr
	};

	check_vkCmdBeginRendering(VK_NULL_HANDLE, &rendering_info);
	assert(f->core13.dynamicRendering == true);
	assert(f->core11.multiview == false);

	rendering_info.viewMask = 1;
	check_vkCmdBeginRendering(VK_NULL_HANDLE, &rendering_info);
	assert(f->core11.multiview == true);
}

static void test_ray_tracing_maintenance1_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> rtm1_exts = { "VK_KHR_ray_tracing_maintenance1" };
	assert(rtm1_exts.size() == 1);
	assert_removed_device_extensions(f, rtm1_exts, { "VK_KHR_ray_tracing_maintenance1" });
	assert(rtm1_exts.empty());

	rtm1_exts.insert("VK_KHR_ray_tracing_maintenance1");
	const char* rtm1_extname = "VK_KHR_ray_tracing_maintenance1";
	const char* rtm1_names[] = { rtm1_extname };
	VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rtm1_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, nullptr, VK_TRUE, VK_FALSE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &rtm1_features };
	dci.ppEnabledExtensionNames = rtm1_names;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_ray_tracing_maintenance1 == true);
	assert_removed_device_extensions(f, rtm1_exts, {});
	assert(rtm1_exts.size() == 1);

	f->has_VK_KHR_ray_tracing_maintenance1.store(false);
	assert_removed_device_extensions(f, rtm1_exts, { "VK_KHR_ray_tracing_maintenance1" });
	assert(rtm1_exts.empty());
	assert_adjusted_device_create_info(f, dci, rtm1_exts, { "VK_KHR_ray_tracing_maintenance1" }, false);
}

static void test_ray_tracing_maintenance1_detection()
{
	feature_detection* f = reset_detection();

	VkQueryPoolCreateInfo qpci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR, 1, 0 };
	check_vkCreateQueryPool(VK_NULL_HANDLE, &qpci, nullptr, nullptr);
	assert(f->has_VK_KHR_ray_tracing_maintenance1 == true);

	f = reset_detection();

	VkMemoryBarrier2 memory_barrier = {
		VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
		VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR,
		VK_PIPELINE_STAGE_2_NONE,
		VK_ACCESS_2_NONE
	};
	VkDependencyInfo dependency_info = {
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0,
		1, &memory_barrier,
		0, nullptr,
		0, nullptr
	};
	check_vkCmdPipelineBarrier2(VK_NULL_HANDLE, &dependency_info);
	assert(f->has_VK_KHR_ray_tracing_maintenance1 == true);

	f = reset_detection();

	check_vkCmdTraceRaysIndirect2KHR(VK_NULL_HANDLE, 0);
	assert(f->has_VK_KHR_ray_tracing_maintenance1 == true);
}

static void test_spirv_extension_detection()
{
	feature_detection* f = reset_detection();

	const uint32_t ray_cull_mask_spirv[] = {
		SpvMagicNumber,
		0x00010000,
		0,
		3,
		0,
		(uint32_t(2) << 16) | SpvOpCapability,
		SpvCapabilityRayCullMaskKHR,
		(uint32_t(3) << 16) | SpvOpMemoryModel,
		SpvAddressingModelLogical,
		SpvMemoryModelGLSL450
	};
	check_shader_module_code(ray_cull_mask_spirv, sizeof(ray_cull_mask_spirv), 1);
	assert(f->has_VK_KHR_ray_tracing_maintenance1 == true);

	f = reset_detection();

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
	std::unordered_set<std::string> multiview_exts = { "VK_KHR_multiview" };
	check_shader_module_code(multiview_spirv, sizeof(multiview_spirv), 2);
	assert(f->has_VK_KHR_multiview == true);
	assert(f->core11.multiview == true);
	assert_removed_device_extensions(f, multiview_exts, {});
	assert(multiview_exts.size() == 1);
}

static void test_vulkan11_multiview_adjustment()
{
	feature_detection* f = reset_detection();

	VkPhysicalDeviceVulkan11Features feat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, nullptr };
	feat11.multiview = VK_TRUE;
	feat11.multiviewGeometryShader = VK_TRUE;
	feat11.multiviewTessellationShader = VK_TRUE;

	auto adjusted = f->adjust_VkPhysicalDeviceVulkan11Features(feat11);
	assert_string_set_equals(adjusted, { "multiview", "multiviewGeometryShader", "multiviewTessellationShader" });
	assert(feat11.multiview == VK_FALSE);
	assert(feat11.multiviewGeometryShader == VK_FALSE);
	assert(feat11.multiviewTessellationShader == VK_FALSE);

	f = reset_detection();
	VkRenderingInfo rendering_info = {
		VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {}, 1, 1, 0, nullptr, nullptr, nullptr
	};
	check_vkCmdBeginRendering(VK_NULL_HANDLE, &rendering_info);
	feat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, nullptr };
	feat11.multiview = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan11Features(feat11);
	assert(adjusted.empty());
	assert(feat11.multiview == VK_TRUE);
}

static void test_vulkan11_multiview_shader_adjustment()
{
	feature_detection* f = reset_detection();

	VkPhysicalDeviceVulkan11Features feat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, nullptr };
	feat11.multiviewGeometryShader = VK_TRUE;
	feat11.multiviewTessellationShader = VK_TRUE;

	auto adjusted = f->adjust_VkPhysicalDeviceVulkan11Features(feat11);
	assert_string_set_equals(adjusted, { "multiviewGeometryShader", "multiviewTessellationShader" });
	assert(feat11.multiviewGeometryShader == VK_FALSE);
	assert(feat11.multiviewTessellationShader == VK_FALSE);

	f = reset_detection();
	VkRenderingInfo rendering_info = {
		VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {}, 1, 1, 0, nullptr, nullptr, nullptr
	};
	check_vkCmdBeginRendering(VK_NULL_HANDLE, &rendering_info);

	VkPipelineShaderStageCreateInfo gs = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_GEOMETRY_BIT };
	struct_check_VkPipelineShaderStageCreateInfo(&gs);
	feat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, nullptr };
	feat11.multiviewGeometryShader = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan11Features(feat11);
	assert(adjusted.empty());
	assert(feat11.multiviewGeometryShader == VK_TRUE);

	f = reset_detection();
	check_vkCmdBeginRendering(VK_NULL_HANDLE, &rendering_info);
	VkPipelineShaderStageCreateInfo tcs = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT };
	struct_check_VkPipelineShaderStageCreateInfo(&tcs);
	feat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, nullptr };
	feat11.multiviewTessellationShader = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan11Features(feat11);
	assert(adjusted.empty());
	assert(feat11.multiviewTessellationShader == VK_TRUE);
}

static void test_large_points_detection()
{
	feature_detection* f = reset_detection();

	const uint32_t plain_vertex_spirv[] = {
		SpvMagicNumber,
		0x00010000,
		0,
		3,
		0,
		(uint32_t(3) << 16) | SpvOpMemoryModel,
		SpvAddressingModelLogical,
		SpvMemoryModelGLSL450
	};
	const uint32_t point_size_spirv[] = {
		SpvMagicNumber,
		0x00010000,
		0,
		3,
		0,
		(uint32_t(3) << 16) | SpvOpMemoryModel,
		SpvAddressingModelLogical,
		SpvMemoryModelGLSL450,
		(uint32_t(4) << 16) | SpvOpDecorate,
		1,
		SpvDecorationBuiltIn,
		SpvBuiltInPointSize
	};

	check_shader_module_code(plain_vertex_spirv, sizeof(plain_vertex_spirv), 3);

	VkPhysicalDeviceFeatures feat10 = {};
	feat10.largePoints = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.largePoints == VK_FALSE);

	f = reset_detection();
	check_shader_module_code(point_size_spirv, sizeof(point_size_spirv), 4);
	feat10 = {};
	feat10.largePoints = VK_TRUE;
	f->adjust_VkPhysicalDeviceFeatures(feat10);
	assert(feat10.largePoints == VK_TRUE);
}

static void test_vulkan14_line_rasterization_adjustment()
{
	feature_detection* f = reset_detection();

	VkPhysicalDeviceVulkan14Features feat14 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, nullptr };
	feat14.rectangularLines = VK_TRUE;
	feat14.bresenhamLines = VK_TRUE;
	feat14.smoothLines = VK_TRUE;
	feat14.stippledRectangularLines = VK_TRUE;
	feat14.stippledBresenhamLines = VK_TRUE;
	feat14.stippledSmoothLines = VK_TRUE;

	auto adjusted = f->adjust_VkPhysicalDeviceVulkan14Features(feat14);
	assert_string_set_equals(adjusted, {
		"rectangularLines",
		"bresenhamLines",
		"smoothLines",
		"stippledRectangularLines",
		"stippledBresenhamLines",
		"stippledSmoothLines",
	});
	assert(feat14.rectangularLines == VK_FALSE);
	assert(feat14.bresenhamLines == VK_FALSE);
	assert(feat14.smoothLines == VK_FALSE);
	assert(feat14.stippledRectangularLines == VK_FALSE);
	assert(feat14.stippledBresenhamLines == VK_FALSE);
	assert(feat14.stippledSmoothLines == VK_FALSE);

	f = reset_detection();
	VkPipelineRasterizationLineStateCreateInfo line_state = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO, nullptr,
		VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH, VK_FALSE, 1, 0xffff
	};
	VkPipelineRasterizationStateCreateInfo raster_state = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, &line_state
	};
	struct_check_VkPipelineRasterizationStateCreateInfo(&raster_state);

	feat14 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, nullptr };
	feat14.smoothLines = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan14Features(feat14);
	assert(adjusted.empty());
	assert(feat14.smoothLines == VK_TRUE);

	f = reset_detection();
	line_state.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_DEFAULT;
	line_state.stippledLineEnable = VK_TRUE;
	struct_check_VkPipelineRasterizationStateCreateInfo(&raster_state);

	feat14 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, nullptr };
	feat14.stippledRectangularLines = VK_TRUE;
	feat14.stippledBresenhamLines = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan14Features(feat14);
	assert_string_set_equals(adjusted, { "stippledBresenhamLines" });
	assert(feat14.stippledRectangularLines == VK_TRUE);
	assert(feat14.stippledBresenhamLines == VK_FALSE);

	f = reset_detection();
	check_vkCmdSetLineRasterizationModeEXT(VK_NULL_HANDLE, VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT);
	feat14 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, nullptr };
	feat14.bresenhamLines = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan14Features(feat14);
	assert(adjusted.empty());
	assert(feat14.bresenhamLines == VK_TRUE);

	f = reset_detection();
	check_vkCmdSetLineStipple(VK_NULL_HANDLE, 1, 0xffff);
	feat14 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, nullptr };
	feat14.stippledRectangularLines = VK_TRUE;
	feat14.stippledBresenhamLines = VK_TRUE;
	feat14.stippledSmoothLines = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan14Features(feat14);
	assert(adjusted.empty());

	f = reset_detection();
	check_vkCmdSetLineStippleEnableEXT(VK_NULL_HANDLE, VK_TRUE);
	feat14 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, nullptr };
	feat14.stippledRectangularLines = VK_TRUE;
	feat14.stippledBresenhamLines = VK_TRUE;
	feat14.stippledSmoothLines = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan14Features(feat14);
	assert(adjusted.empty());
}

static void test_buffer_device_address_shader_module()
{
	reset_detection();
	check_shader_module_code((const uint32_t*)vulkan_compute_bda_sc_spirv,
	                         long(ceil(vulkan_compute_bda_sc_spirv_len / 4.0)) * sizeof(uint32_t),
	                         5);
}

int main()
{
	test_logic_op_adjustment();
	test_texture_compression_adjustment();
	test_vulkan12_adjustment();
	test_extension_chain_helpers();
	test_shader_atomic_int64_extension_adjustment();
	test_map_memory2_extension_adjustment();
	test_robustness2_extension_adjustment();
	test_multiview_extension_adjustment();
	test_multiview_render_pass_detection();
	test_multiview_render_pass2_detection();
	test_multiview_dynamic_rendering_detection();
	test_ray_tracing_maintenance1_extension_adjustment();
	test_ray_tracing_maintenance1_detection();
	test_spirv_extension_detection();
	test_vulkan11_multiview_adjustment();
	test_vulkan11_multiview_shader_adjustment();
	test_large_points_detection();
	test_vulkan14_line_rasterization_adjustment();
	test_buffer_device_address_shader_module();
	return 0;
}
