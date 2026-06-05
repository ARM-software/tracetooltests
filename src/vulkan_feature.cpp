#include "vulkan_utility.h"
#include "src/usagetracker/vulkan_feature_detect.h"
#include "vulkan_compute_bda_sc.inc"
#include "vulkan_rayquery.frag.inc"
#include "vulkan_transform_feedback_vert.inc"
#include "vulkan_demo_descriptor_indexing_frag.inc"

#include <array>
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

static void assert_removed_instance_extensions(feature_detection* f, std::unordered_set<std::string>& exts, std::initializer_list<const char*> expected_removed)
{
	auto removed = f->adjust_instance_extensions(exts);
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

static void test_bind_memory2_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_bind_memory2" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_bind_memory2 == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_bind_memory2" });
	assert(exts.empty());

	VkBindBufferMemoryInfo buffer_info = {
		VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, 0
	};
	VkBindImageMemoryInfo image_info = {
		VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, 0
	};

	VkResult result = check_vkBindBufferMemory2(VK_NULL_HANDLE, 1, &buffer_info);
	assert(result == VK_SUCCESS);
	result = check_vkBindImageMemory2(VK_NULL_HANDLE, 1, &image_info);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_KHR_bind_memory2 == false);

	result = check_vkBindBufferMemory2KHR(VK_NULL_HANDLE, 1, &buffer_info);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_KHR_bind_memory2 == true);
	exts.insert("VK_KHR_bind_memory2");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f->has_VK_KHR_bind_memory2.store(false);
	result = check_vkBindImageMemory2KHR(VK_NULL_HANDLE, 1, &image_info);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_KHR_bind_memory2 == true);
}

static void test_copy_commands2_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_copy_commands2" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_copy_commands2 == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_copy_commands2" });
	assert(exts.empty());

	VkBufferCopy2 buffer_region = { VK_STRUCTURE_TYPE_BUFFER_COPY_2, nullptr, 0, 0, 16 };
	VkCopyBufferInfo2 copy_buffer_info = {
		VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2, nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &buffer_region
	};
	check_vkCmdCopyBuffer2(VK_NULL_HANDLE, &copy_buffer_info);
	assert(f->has_VK_KHR_copy_commands2 == false);

	check_vkCmdCopyBuffer2KHR(VK_NULL_HANDLE, &copy_buffer_info);
	assert(f->has_VK_KHR_copy_commands2 == true);
	exts.insert("VK_KHR_copy_commands2");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	VkImageSubresourceLayers subresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	VkExtent3D extent = { 1, 1, 1 };

	f->has_VK_KHR_copy_commands2.store(false);
	VkImageCopy2 image_region = { VK_STRUCTURE_TYPE_IMAGE_COPY_2, nullptr };
	image_region.srcSubresource = subresource;
	image_region.dstSubresource = subresource;
	image_region.extent = extent;
	VkCopyImageInfo2 copy_image_info = {
		VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2, nullptr,
		VK_NULL_HANDLE, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_NULL_HANDLE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &image_region
	};
	check_vkCmdCopyImage2KHR(VK_NULL_HANDLE, &copy_image_info);
	assert(f->has_VK_KHR_copy_commands2 == true);

	f->has_VK_KHR_copy_commands2.store(false);
	VkBufferImageCopy2 buffer_image_region = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2, nullptr };
	buffer_image_region.imageSubresource = subresource;
	buffer_image_region.imageExtent = extent;
	VkCopyBufferToImageInfo2 copy_buffer_to_image_info = {
		VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2, nullptr,
		VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &buffer_image_region
	};
	check_vkCmdCopyBufferToImage2KHR(VK_NULL_HANDLE, &copy_buffer_to_image_info);
	assert(f->has_VK_KHR_copy_commands2 == true);

	f->has_VK_KHR_copy_commands2.store(false);
	VkCopyImageToBufferInfo2 copy_image_to_buffer_info = {
		VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2, nullptr,
		VK_NULL_HANDLE, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_NULL_HANDLE,
		1, &buffer_image_region
	};
	check_vkCmdCopyImageToBuffer2KHR(VK_NULL_HANDLE, &copy_image_to_buffer_info);
	assert(f->has_VK_KHR_copy_commands2 == true);

	f->has_VK_KHR_copy_commands2.store(false);
	VkOffset3D image_offsets[2] = { { 0, 0, 0 }, { 1, 1, 1 } };
	VkImageBlit2 blit_region = { VK_STRUCTURE_TYPE_IMAGE_BLIT_2, nullptr };
	blit_region.srcSubresource = subresource;
	blit_region.dstSubresource = subresource;
	blit_region.srcOffsets[0] = image_offsets[0];
	blit_region.srcOffsets[1] = image_offsets[1];
	blit_region.dstOffsets[0] = image_offsets[0];
	blit_region.dstOffsets[1] = image_offsets[1];
	VkBlitImageInfo2 blit_info = {
		VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, nullptr,
		VK_NULL_HANDLE, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_NULL_HANDLE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit_region, VK_FILTER_NEAREST
	};
	check_vkCmdBlitImage2KHR(VK_NULL_HANDLE, &blit_info);
	assert(f->has_VK_KHR_copy_commands2 == true);

	f->has_VK_KHR_copy_commands2.store(false);
	VkImageResolve2 resolve_region = { VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2, nullptr };
	resolve_region.srcSubresource = subresource;
	resolve_region.dstSubresource = subresource;
	resolve_region.extent = extent;
	VkResolveImageInfo2 resolve_info = {
		VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2, nullptr,
		VK_NULL_HANDLE, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_NULL_HANDLE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &resolve_region
	};
	check_vkCmdResolveImage2KHR(VK_NULL_HANDLE, &resolve_info);
	assert(f->has_VK_KHR_copy_commands2 == true);
}

static void test_create_renderpass2_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_create_renderpass2" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_create_renderpass2 == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_create_renderpass2" });
	assert(exts.empty());

	VkAttachmentDescription2 attachment = { VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2, nullptr };
	attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference2 color_reference = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr };
	color_reference.attachment = 0;
	color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color_reference.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	VkSubpassDescription2 subpasses[2] = {};
	for (VkSubpassDescription2& subpass : subpasses)
	{
		subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_reference;
	}

	VkSubpassDependency2 dependencies[2] = {};
	for (uint32_t i = 0; i < 2; i++)
	{
		dependencies[i].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
		dependencies[i].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[i].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[i].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = 1;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo2 render_pass_info = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2, nullptr, 0, 1, &attachment, 2, subpasses, 2, dependencies, 0, nullptr
	};

	check_vkCreateRenderPass2(VK_NULL_HANDLE, &render_pass_info, nullptr, nullptr);
	assert(f->has_VK_KHR_create_renderpass2 == false);

	check_vkCreateRenderPass2KHR(VK_NULL_HANDLE, &render_pass_info, nullptr, nullptr);
	assert(f->has_VK_KHR_create_renderpass2 == true);
	exts.insert("VK_KHR_create_renderpass2");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f->has_VK_KHR_create_renderpass2.store(false);
	VkRenderPassBeginInfo begin_render_pass = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
	VkSubpassBeginInfo begin_info = { VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO, nullptr, VK_SUBPASS_CONTENTS_INLINE };
	VkSubpassEndInfo end_info = { VK_STRUCTURE_TYPE_SUBPASS_END_INFO, nullptr };

	check_vkCmdBeginRenderPass2(VK_NULL_HANDLE, &begin_render_pass, &begin_info);
	assert(f->has_VK_KHR_create_renderpass2 == false);

	check_vkCmdBeginRenderPass2KHR(VK_NULL_HANDLE, &begin_render_pass, &begin_info);
	assert(f->has_VK_KHR_create_renderpass2 == true);

	f->has_VK_KHR_create_renderpass2.store(false);
	check_vkCmdNextSubpass2KHR(VK_NULL_HANDLE, &begin_info, &end_info);
	assert(f->has_VK_KHR_create_renderpass2 == true);

	f->has_VK_KHR_create_renderpass2.store(false);
	check_vkCmdEndRenderPass2KHR(VK_NULL_HANDLE, &end_info);
	assert(f->has_VK_KHR_create_renderpass2 == true);
}

static void test_dynamic_rendering_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_dynamic_rendering" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_dynamic_rendering == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_dynamic_rendering" });
	assert(exts.empty());

	const char* extname = "VK_KHR_dynamic_rendering";
	const char* namelist[] = { extname };
	VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR, nullptr, VK_TRUE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &dynamic_rendering };
	dci.ppEnabledExtensionNames = namelist;
	dci.enabledExtensionCount = 1;

	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
	ici.pApplicationInfo = &app_info;

	app_info.apiVersion = VK_API_VERSION_1_2;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_dynamic_rendering == true);
	exts.insert("VK_KHR_dynamic_rendering");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
	assert_adjusted_device_create_info(f, dci, exts, {}, true);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_3;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	exts = { "VK_KHR_dynamic_rendering" };
	dynamic_rendering = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR, nullptr, VK_TRUE };
	dci.pNext = &dynamic_rendering;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_dynamic_rendering == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_dynamic_rendering" });
	assert(exts.empty());
	assert_adjusted_device_create_info(f, dci, exts, { "VK_KHR_dynamic_rendering" }, false);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_2;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
	VkPipelineRenderingCreateInfoKHR pipeline_rendering = {
		VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR, nullptr, 0, 1, &color_format, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED
	};
	VkGraphicsPipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &pipeline_rendering };
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = check_vkCreateGraphicsPipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_KHR_dynamic_rendering == true);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_3;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	pipeline_rendering = {
		VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR, nullptr, 0, 1, &color_format, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED
	};
	pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &pipeline_rendering };
	result = check_vkCreateGraphicsPipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_KHR_dynamic_rendering == false);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_2;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkCommandBufferInheritanceRenderingInfoKHR inheritance_rendering = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO_KHR, nullptr, 0, 0, 1, &color_format, VK_FORMAT_UNDEFINED,
		VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT
	};
	VkCommandBufferInheritanceInfo inheritance = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, &inheritance_rendering };
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
	begin_info.pInheritanceInfo = &inheritance;
	special_vkBeginCommandBuffer(VK_NULL_HANDLE, &begin_info, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
	assert(f->has_VK_KHR_dynamic_rendering == true);
	assert(f->core13.dynamicRendering == true);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_3;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkRenderingInfoKHR rendering_info = {
		VK_STRUCTURE_TYPE_RENDERING_INFO_KHR, nullptr, 0, {}, 1, 0, 0, nullptr, nullptr, nullptr
	};
	check_vkCmdBeginRendering(VK_NULL_HANDLE, &rendering_info);
	assert(f->core13.dynamicRendering == true);
	assert(f->has_VK_KHR_dynamic_rendering == false);

	check_vkCmdBeginRenderingKHR(VK_NULL_HANDLE, &rendering_info);
	assert(f->has_VK_KHR_dynamic_rendering == true);

	f->has_VK_KHR_dynamic_rendering.store(false);
	check_vkCmdEndRenderingKHR(VK_NULL_HANDLE);
	assert(f->has_VK_KHR_dynamic_rendering == true);
}

static void test_synchronization2_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_synchronization2" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_synchronization2 == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_synchronization2" });
	assert(exts.empty());

	const char* extname = "VK_KHR_synchronization2";
	const char* namelist[] = { extname };
	VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, nullptr, VK_TRUE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &synchronization2 };
	dci.ppEnabledExtensionNames = namelist;
	dci.enabledExtensionCount = 1;

	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
	ici.pApplicationInfo = &app_info;

	app_info.apiVersion = VK_API_VERSION_1_2;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_synchronization2 == true);
	exts.insert("VK_KHR_synchronization2");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
	assert_adjusted_device_create_info(f, dci, exts, {}, true);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_3;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	exts = { "VK_KHR_synchronization2" };
	synchronization2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, nullptr, VK_TRUE };
	dci.pNext = &synchronization2;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_synchronization2 == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_synchronization2" });
	assert(exts.empty());
	assert_adjusted_device_create_info(f, dci, exts, { "VK_KHR_synchronization2" }, false);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_2;
	check_vkCreateInstance(&ici, nullptr, nullptr);

	VkPhysicalDeviceVulkan13Features feat13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, nullptr };
	feat13.synchronization2 = VK_TRUE;
	auto adjusted = f->adjust_VkPhysicalDeviceVulkan13Features(feat13);
	assert_string_set_equals(adjusted, { "synchronization2" });
	assert(feat13.synchronization2 == VK_FALSE);

	VkMemoryBarrier2 memory_barrier = {
		VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr,
		VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT
	};
	VkDependencyInfo dependency_info = {
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0,
		1, &memory_barrier,
		0, nullptr,
		0, nullptr
	};

	check_vkCmdSetEvent2(VK_NULL_HANDLE, VK_NULL_HANDLE, &dependency_info);
	assert(f->core13.synchronization2 == true);
	assert(f->has_VK_KHR_synchronization2 == true);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	check_vkCmdResetEvent2(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_PIPELINE_STAGE_2_COPY_BIT);
	assert(f->core13.synchronization2 == true);
	assert(f->has_VK_KHR_synchronization2 == true);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkEvent events[] = { VK_NULL_HANDLE };
	check_vkCmdWaitEvents2(VK_NULL_HANDLE, 1, events, &dependency_info);
	assert(f->core13.synchronization2 == true);
	assert(f->has_VK_KHR_synchronization2 == true);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	check_vkCmdPipelineBarrier2(VK_NULL_HANDLE, &dependency_info);
	assert(f->core13.synchronization2 == true);
	assert(f->has_VK_KHR_synchronization2 == true);

	feat13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, nullptr };
	feat13.synchronization2 = VK_TRUE;
	adjusted = f->adjust_VkPhysicalDeviceVulkan13Features(feat13);
	assert(adjusted.empty());
	assert(feat13.synchronization2 == VK_TRUE);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	check_vkCmdWriteTimestamp2(VK_NULL_HANDLE, VK_PIPELINE_STAGE_2_COPY_BIT, VK_NULL_HANDLE, 0);
	assert(f->core13.synchronization2 == true);
	assert(f->has_VK_KHR_synchronization2 == true);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkCommandBufferSubmitInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, VK_NULL_HANDLE, 0 };
	VkSemaphoreSubmitInfo signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr };
	signal_info.semaphore = VK_NULL_HANDLE;
	signal_info.value = 0;
	signal_info.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	signal_info.deviceIndex = 0;
	VkSubmitInfo2 submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 0, nullptr, 1, &command_buffer_info, 1, &signal_info };
	VkResult result = check_vkQueueSubmit2(VK_NULL_HANDLE, 1, &submit_info, VK_NULL_HANDLE);
	assert(result == VK_SUCCESS);
	assert(f->core13.synchronization2 == true);
	assert(f->has_VK_KHR_synchronization2 == true);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_3;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	check_vkCmdPipelineBarrier2(VK_NULL_HANDLE, &dependency_info);
	assert(f->core13.synchronization2 == true);
	assert(f->has_VK_KHR_synchronization2 == false);
	check_vkCmdPipelineBarrier2KHR(VK_NULL_HANDLE, &dependency_info);
	assert(f->has_VK_KHR_synchronization2 == true);
}

static void test_transform_feedback_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_EXT_transform_feedback" };
	assert(exts.size() == 1);
	assert(f->has_VK_EXT_transform_feedback == false);
	assert_removed_device_extensions(f, exts, { "VK_EXT_transform_feedback" });
	assert(exts.empty());

	VkPhysicalDeviceTransformFeedbackFeaturesEXT tf_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, nullptr, VK_TRUE, VK_FALSE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &tf_features };
	const char* extname = "VK_EXT_transform_feedback";
	const char* namelist[] = { extname };
	dci.ppEnabledExtensionNames = namelist;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_EXT_transform_feedback == true);
	exts.insert("VK_EXT_transform_feedback");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
	assert_adjusted_device_create_info(f, dci, exts, {}, true);

	f->has_VK_EXT_transform_feedback.store(false);
	tf_features.transformFeedback = VK_FALSE;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_EXT_transform_feedback == false);
	assert_removed_device_extensions(f, exts, { "VK_EXT_transform_feedback" });
	assert(exts.empty());
	assert_adjusted_device_create_info(f, dci, exts, { "VK_EXT_transform_feedback" }, false);

	f = reset_detection();
	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = 64;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
	check_vkCreateBuffer(VK_NULL_HANDLE, &buffer_info, nullptr, nullptr);
	assert(f->has_VK_EXT_transform_feedback == true);

	f->has_VK_EXT_transform_feedback.store(false);
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
	check_vkCreateBuffer(VK_NULL_HANDLE, &buffer_info, nullptr, nullptr);
	assert(f->has_VK_EXT_transform_feedback == true);

	f->has_VK_EXT_transform_feedback.store(false);
	VkQueryPoolCreateInfo query_pool_info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr };
	query_pool_info.queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
	query_pool_info.queryCount = 1;
	VkResult result = check_vkCreateQueryPool(VK_NULL_HANDLE, &query_pool_info, nullptr, nullptr);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_EXT_transform_feedback == true);

	f->has_VK_EXT_transform_feedback.store(false);
	VkPipelineRasterizationStateStreamCreateInfoEXT stream_info = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT, nullptr, 0, 0
	};
	VkPipelineRasterizationStateCreateInfo raster_state = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, &stream_info
	};
	struct_check_VkPipelineRasterizationStateCreateInfo(&raster_state);
	assert(f->has_VK_EXT_transform_feedback == true);

	std::array<VkBuffer, 1> buffers = { VK_NULL_HANDLE };
	std::array<VkDeviceSize, 1> offsets = { 0 };
	std::array<VkDeviceSize, 1> sizes = { 64 };

	f->has_VK_EXT_transform_feedback.store(false);
	check_vkCmdBindTransformFeedbackBuffersEXT(VK_NULL_HANDLE, 0, 1, buffers.data(), offsets.data(), sizes.data());
	assert(f->has_VK_EXT_transform_feedback == true);

	f->has_VK_EXT_transform_feedback.store(false);
	check_vkCmdBeginTransformFeedbackEXT(VK_NULL_HANDLE, 0, 1, buffers.data(), offsets.data());
	assert(f->has_VK_EXT_transform_feedback == true);

	f->has_VK_EXT_transform_feedback.store(false);
	check_vkCmdEndTransformFeedbackEXT(VK_NULL_HANDLE, 0, 1, buffers.data(), offsets.data());
	assert(f->has_VK_EXT_transform_feedback == true);

	f->has_VK_EXT_transform_feedback.store(false);
	check_vkCmdBeginQueryIndexedEXT(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0, 0);
	assert(f->has_VK_EXT_transform_feedback == true);

	f->has_VK_EXT_transform_feedback.store(false);
	check_vkCmdEndQueryIndexedEXT(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0);
	assert(f->has_VK_EXT_transform_feedback == true);

	f->has_VK_EXT_transform_feedback.store(false);
	check_vkCmdDrawIndirectByteCountEXT(VK_NULL_HANDLE, 1, 0, VK_NULL_HANDLE, 0, 0, 16);
	assert(f->has_VK_EXT_transform_feedback == true);
}

static void test_descriptor_indexing_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_EXT_descriptor_indexing" };
	assert(exts.size() == 1);
	assert(f->has_VK_EXT_descriptor_indexing == false);
	assert_removed_device_extensions(f, exts, { "VK_EXT_descriptor_indexing" });
	assert(exts.empty());

	const char* extname = "VK_EXT_descriptor_indexing";
	const char* namelist[] = { extname };
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptor_indexing = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT, nullptr
	};
	descriptor_indexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	descriptor_indexing.runtimeDescriptorArray = VK_TRUE;
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &descriptor_indexing };
	dci.ppEnabledExtensionNames = namelist;
	dci.enabledExtensionCount = 1;

	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
	ici.pApplicationInfo = &app_info;

	app_info.apiVersion = VK_API_VERSION_1_1;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_EXT_descriptor_indexing == true);
	exts.insert("VK_EXT_descriptor_indexing");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
	assert_adjusted_device_create_info(f, dci, exts, {}, true);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_2;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	exts = { "VK_EXT_descriptor_indexing" };
	descriptor_indexing = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT, nullptr };
	descriptor_indexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	dci.pNext = &descriptor_indexing;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_EXT_descriptor_indexing == false);
	assert_removed_device_extensions(f, exts, { "VK_EXT_descriptor_indexing" });
	assert(exts.empty());
	assert_adjusted_device_create_info(f, dci, exts, { "VK_EXT_descriptor_indexing" }, false);

	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_2;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	descriptor_indexing = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT, nullptr };
	descriptor_indexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	dci.pNext = &descriptor_indexing;
	dci.ppEnabledExtensionNames = nullptr;
	dci.enabledExtensionCount = 0;
	check_shader_module_code((const uint32_t*)vulkan_demo_descriptor_indexing_frag_spv,
	                         long(ceil(vulkan_demo_descriptor_indexing_frag_spv_len / 4.0)) * sizeof(uint32_t),
	                         9);
	assert(f->has_VK_EXT_descriptor_indexing == false);
	std::unordered_set<std::string> no_exts;
	assert_adjusted_device_create_info(f, dci, no_exts, {}, true);

	dci.ppEnabledExtensionNames = namelist;
	dci.enabledExtensionCount = 1;
	f = reset_detection();
	app_info.apiVersion = VK_API_VERSION_1_1;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkDescriptorSetLayoutBinding bindings[2] = {
		{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
	};
	VkDescriptorBindingFlags binding_flags[2] = {
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
	};
	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr, 2, binding_flags
	};
	VkDescriptorSetLayoutCreateInfo layout_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &binding_flags_info,
		VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		2, bindings
	};
	VkResult result = check_vkCreateDescriptorSetLayout(VK_NULL_HANDLE, &layout_info, nullptr, nullptr);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_EXT_descriptor_indexing == true);
	assert(f->core12.descriptorIndexing == true);
	assert(f->core12.descriptorBindingStorageBufferUpdateAfterBind == true);
	assert(f->core12.descriptorBindingSampledImageUpdateAfterBind == true);
	assert(f->core12.descriptorBindingPartiallyBound == true);
	assert(f->core12.descriptorBindingVariableDescriptorCount == true);

	VkPhysicalDeviceVulkan12Features feat12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, nullptr };
	feat12.descriptorIndexing = VK_TRUE;
	feat12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
	feat12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	feat12.descriptorBindingPartiallyBound = VK_TRUE;
	feat12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	auto adjusted = f->adjust_VkPhysicalDeviceVulkan12Features(feat12);
	assert(adjusted.empty());

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkDescriptorPoolCreateInfo pool_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr,
		VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		1, 0, nullptr
	};
	result = check_vkCreateDescriptorPool(VK_NULL_HANDLE, &pool_info, nullptr, nullptr);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_EXT_descriptor_indexing == true);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	uint32_t variable_count = 4;
	VkDescriptorSetVariableDescriptorCountAllocateInfo variable_allocate = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT, nullptr, 1, &variable_count
	};
	VkDescriptorSetAllocateInfo allocate_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, &variable_allocate,
		VK_NULL_HANDLE, 0, nullptr
	};
	result = check_vkAllocateDescriptorSets(VK_NULL_HANDLE, &allocate_info, nullptr);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_EXT_descriptor_indexing == true);
	assert(f->core12.descriptorBindingVariableDescriptorCount == true);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkDescriptorSetVariableDescriptorCountLayoutSupport variable_support = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT_EXT, nullptr, 0
	};
	VkDescriptorSetLayoutSupport support = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT, &variable_support, VK_FALSE
	};
	check_vkGetDescriptorSetLayoutSupport(VK_NULL_HANDLE, &layout_info, &support);
	assert(f->has_VK_EXT_descriptor_indexing == true);
	assert(f->core12.descriptorBindingVariableDescriptorCount == true);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	VkDescriptorSetLayoutBinding plain_binding = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	VkDescriptorSetLayoutCreateInfo plain_layout_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &plain_binding
	};
	check_vkGetDescriptorSetLayoutSupport(VK_NULL_HANDLE, &plain_layout_info, &support);
	assert(f->has_VK_EXT_descriptor_indexing == false);
	assert(f->core12.descriptorBindingVariableDescriptorCount == false);

	f = reset_detection();
	check_vkCreateInstance(&ici, nullptr, nullptr);
	check_shader_module_code((const uint32_t*)vulkan_demo_descriptor_indexing_frag_spv,
	                         long(ceil(vulkan_demo_descriptor_indexing_frag_spv_len / 4.0)) * sizeof(uint32_t),
	                         7);
	assert(f->has_VK_EXT_descriptor_indexing == true);
	assert(f->core12.descriptorIndexing == true);
	assert(f->core12.shaderSampledImageArrayNonUniformIndexing == true);
}

static void test_get_physical_device_properties2_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_get_physical_device_properties2" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_get_physical_device_properties2 == false);
	assert_removed_instance_extensions(f, exts, { "VK_KHR_get_physical_device_properties2" });
	assert(exts.empty());

	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, nullptr };
	VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, nullptr };
	VkFormatProperties2 format_properties = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, nullptr };
	VkPhysicalDeviceImageFormatInfo2 image_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, nullptr,
		VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0
	};
	VkImageFormatProperties2 image_properties = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, nullptr };
	VkPhysicalDeviceMemoryProperties2 memory_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, nullptr };
	VkPhysicalDeviceSparseImageFormatInfo2 sparse_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_INFO_2, nullptr,
		VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_TILING_OPTIMAL
	};
	uint32_t queue_family_count = 0;
	uint32_t sparse_property_count = 0;

	check_vkGetPhysicalDeviceFeatures2KHR(VK_NULL_HANDLE, &features);
	assert(f->has_VK_KHR_get_physical_device_properties2 == true);

	exts.insert("VK_KHR_get_physical_device_properties2");
	assert_removed_instance_extensions(f, exts, {});
	assert(exts.size() == 1);

	f->has_VK_KHR_get_physical_device_properties2.store(false);
	check_vkGetPhysicalDeviceProperties2KHR(VK_NULL_HANDLE, &properties);
	assert(f->has_VK_KHR_get_physical_device_properties2 == true);

	f->has_VK_KHR_get_physical_device_properties2.store(false);
	check_vkGetPhysicalDeviceFormatProperties2KHR(VK_NULL_HANDLE, VK_FORMAT_R8G8B8A8_UNORM, &format_properties);
	assert(f->has_VK_KHR_get_physical_device_properties2 == true);

	f->has_VK_KHR_get_physical_device_properties2.store(false);
	VkResult result = check_vkGetPhysicalDeviceImageFormatProperties2KHR(VK_NULL_HANDLE, &image_info, &image_properties);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_KHR_get_physical_device_properties2 == true);

	f->has_VK_KHR_get_physical_device_properties2.store(false);
	check_vkGetPhysicalDeviceQueueFamilyProperties2KHR(VK_NULL_HANDLE, &queue_family_count, nullptr);
	assert(f->has_VK_KHR_get_physical_device_properties2 == true);

	f->has_VK_KHR_get_physical_device_properties2.store(false);
	check_vkGetPhysicalDeviceMemoryProperties2KHR(VK_NULL_HANDLE, &memory_properties);
	assert(f->has_VK_KHR_get_physical_device_properties2 == true);

	f->has_VK_KHR_get_physical_device_properties2.store(false);
	check_vkGetPhysicalDeviceSparseImageFormatProperties2KHR(VK_NULL_HANDLE, &sparse_info, &sparse_property_count, nullptr);
	assert(f->has_VK_KHR_get_physical_device_properties2 == true);
}

static void test_external_fence_capabilities_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_external_fence_capabilities" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_external_fence_capabilities == false);
	assert_removed_instance_extensions(f, exts, { "VK_KHR_external_fence_capabilities" });
	assert(exts.empty());

	VkPhysicalDeviceExternalFenceInfo external_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO, nullptr, VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT
	};
	VkExternalFenceProperties external_properties = { VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES, nullptr };

	check_vkGetPhysicalDeviceExternalFencePropertiesKHR(VK_NULL_HANDLE, &external_info, &external_properties);
	assert(f->has_VK_KHR_external_fence_capabilities == true);

	exts.insert("VK_KHR_external_fence_capabilities");
	assert_removed_instance_extensions(f, exts, {});
	assert(exts.size() == 1);
}

static void test_get_memory_requirements2_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_get_memory_requirements2" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_get_memory_requirements2 == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_get_memory_requirements2" });
	assert(exts.empty());

	VkBufferMemoryRequirementsInfo2 buffer_info = {
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2, nullptr, VK_NULL_HANDLE
	};
	VkImageMemoryRequirementsInfo2 image_info = {
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, nullptr, VK_NULL_HANDLE
	};
	VkImageSparseMemoryRequirementsInfo2 sparse_info = {
		VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2, nullptr, VK_NULL_HANDLE
	};
	VkMemoryRequirements2 memory_requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
	uint32_t sparse_requirement_count = 0;

	check_vkGetBufferMemoryRequirements2(VK_NULL_HANDLE, &buffer_info, &memory_requirements);
	check_vkGetImageMemoryRequirements2(VK_NULL_HANDLE, &image_info, &memory_requirements);
	check_vkGetImageSparseMemoryRequirements2(VK_NULL_HANDLE, &sparse_info, &sparse_requirement_count, nullptr);
	assert(f->has_VK_KHR_get_memory_requirements2 == false);

	check_vkGetBufferMemoryRequirements2KHR(VK_NULL_HANDLE, &buffer_info, &memory_requirements);
	assert(f->has_VK_KHR_get_memory_requirements2 == true);
	exts.insert("VK_KHR_get_memory_requirements2");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f->has_VK_KHR_get_memory_requirements2.store(false);
	check_vkGetImageMemoryRequirements2KHR(VK_NULL_HANDLE, &image_info, &memory_requirements);
	assert(f->has_VK_KHR_get_memory_requirements2 == true);

	f->has_VK_KHR_get_memory_requirements2.store(false);
	check_vkGetImageSparseMemoryRequirements2KHR(VK_NULL_HANDLE, &sparse_info, &sparse_requirement_count, nullptr);
	assert(f->has_VK_KHR_get_memory_requirements2 == true);
}

static void test_external_memory_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_external_memory" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_external_memory == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_external_memory" });
	assert(exts.empty());

	exts.insert("VK_KHR_external_memory");

	VkExternalMemoryBufferCreateInfo buffer_external_info = {
		VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, nullptr, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
	};
	VkBufferCreateInfo buffer_info = {
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &buffer_external_info, 0, 64,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr
	};
	check_vkCreateBuffer(VK_NULL_HANDLE, &buffer_info, nullptr, nullptr);
	assert(f->has_VK_KHR_external_memory == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f = reset_detection();
	exts = { "VK_KHR_external_memory" };

	VkExternalMemoryImageCreateInfo image_external_info = {
		VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, nullptr, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
	};
	VkImageCreateInfo image_info = {
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &image_external_info, 0,
		VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { 4, 4, 1 },
		1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr,
		VK_IMAGE_LAYOUT_UNDEFINED
	};
	check_vkCreateImage(VK_NULL_HANDLE, &image_info, nullptr, nullptr);
	assert(f->has_VK_KHR_external_memory == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f = reset_detection();
	exts = { "VK_KHR_external_memory" };

	VkExportMemoryAllocateInfo export_info = {
		VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, nullptr, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
	};
	VkMemoryAllocateInfo alloc_info = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &export_info, 4096, 0
	};
	check_vkAllocateMemory(VK_NULL_HANDLE, &alloc_info, nullptr, nullptr);
	assert(f->has_VK_KHR_external_memory == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
}

static void test_external_memory_fd_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_KHR_external_memory_fd" };
	assert(exts.size() == 1);
	assert(f->has_VK_KHR_external_memory_fd == false);
	assert_removed_device_extensions(f, exts, { "VK_KHR_external_memory_fd" });
	assert(exts.empty());

	exts.insert("VK_KHR_external_memory_fd");

	VkMemoryGetFdInfoKHR get_fd_info = {
		VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, nullptr,
		VK_NULL_HANDLE, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
	};
	check_vkGetMemoryFdKHR(VK_NULL_HANDLE, &get_fd_info, nullptr);
	assert(f->has_VK_KHR_external_memory_fd == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f = reset_detection();
	exts = { "VK_KHR_external_memory_fd" };

	VkMemoryFdPropertiesKHR fd_properties = {
		VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR, nullptr, 0
	};
	check_vkGetMemoryFdPropertiesKHR(
		VK_NULL_HANDLE,
		VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
		-1,
		&fd_properties);
	assert(f->has_VK_KHR_external_memory_fd == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f = reset_detection();
	exts = { "VK_KHR_external_memory_fd" };

	VkImportMemoryFdInfoKHR import_info = {
		VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, nullptr,
		VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, -1
	};
	VkMemoryAllocateInfo alloc_info = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import_info, 4096, 0
	};
	check_vkAllocateMemory(VK_NULL_HANDLE, &alloc_info, nullptr, nullptr);
	assert(f->has_VK_KHR_external_memory_fd == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
}

static void test_external_memory_host_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_EXT_external_memory_host" };
	assert(exts.size() == 1);
	assert(f->has_VK_EXT_external_memory_host == false);
	assert_removed_device_extensions(f, exts, { "VK_EXT_external_memory_host" });
	assert(exts.empty());

	exts.insert("VK_EXT_external_memory_host");

	int host_value = 17;
	VkImportMemoryHostPointerInfoEXT import_info = {
		VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT, nullptr,
		VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, &host_value
	};
	VkMemoryAllocateInfo alloc_info = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import_info, 4096, 0
	};
	check_vkAllocateMemory(VK_NULL_HANDLE, &alloc_info, nullptr, nullptr);
	assert(f->has_VK_EXT_external_memory_host == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);

	f = reset_detection();
	exts = { "VK_EXT_external_memory_host" };

	VkMemoryHostPointerPropertiesEXT pointer_props = {
		VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT, nullptr, 0
	};
	check_vkGetMemoryHostPointerPropertiesEXT(
		VK_NULL_HANDLE,
		VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
		&host_value,
		&pointer_props);
	assert(f->has_VK_EXT_external_memory_host == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
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

static void test_maintenance1_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> maintenance1_exts = { "VK_KHR_maintenance1" };
	assert(maintenance1_exts.size() == 1);
	assert_removed_device_extensions(f, maintenance1_exts, {});
	assert(maintenance1_exts.size() == 1);

	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
	ici.pApplicationInfo = &app_info;

	check_vkCreateInstance(&ici, nullptr, nullptr);
	assert(f->requested_instance_api_version.load() == VK_API_VERSION_1_0);
	assert_removed_device_extensions(f, maintenance1_exts, {});
	assert(maintenance1_exts.size() == 1);

	f = reset_detection();
	maintenance1_exts = { "VK_KHR_maintenance1" };
	app_info.apiVersion = VK_API_VERSION_1_1;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	assert(f->requested_instance_api_version.load() == VK_API_VERSION_1_1);
	assert_removed_device_extensions(f, maintenance1_exts, { "VK_KHR_maintenance1" });
	assert(maintenance1_exts.empty());

	f = reset_detection();
	maintenance1_exts = { "VK_KHR_maintenance1" };
	app_info.apiVersion = VK_API_VERSION_1_2;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	app_info.apiVersion = VK_API_VERSION_1_0;
	check_vkCreateInstance(&ici, nullptr, nullptr);
	assert(f->requested_instance_api_version.load() == VK_API_VERSION_1_0);
	assert_removed_device_extensions(f, maintenance1_exts, {});
	assert(maintenance1_exts.size() == 1);
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
	assert(f->has_VK_KHR_ray_tracing_maintenance1 == false);
	assert_removed_device_extensions(f, rtm1_exts, { "VK_KHR_ray_tracing_maintenance1" });
	assert(rtm1_exts.empty());
	assert_adjusted_device_create_info(f, dci, rtm1_exts, { "VK_KHR_ray_tracing_maintenance1" }, false);
}

static void test_ray_tracing_pipeline_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> rtp_exts = { "VK_KHR_ray_tracing_pipeline" };
	assert(rtp_exts.size() == 1);
	assert_removed_device_extensions(f, rtp_exts, { "VK_KHR_ray_tracing_pipeline" });
	assert(rtp_exts.empty());

	rtp_exts.insert("VK_KHR_ray_tracing_pipeline");
	const char* rtp_extname = "VK_KHR_ray_tracing_pipeline";
	const char* rtp_names[] = { rtp_extname };
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtp_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &rtp_features };
	dci.ppEnabledExtensionNames = rtp_names;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);
	assert_removed_device_extensions(f, rtp_exts, {});
	assert(rtp_exts.size() == 1);
	assert_adjusted_device_create_info(f, dci, rtp_exts, {}, true);
}

static void test_acceleration_structure_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> as_exts = { "VK_KHR_acceleration_structure" };
	assert(as_exts.size() == 1);
	assert_removed_device_extensions(f, as_exts, { "VK_KHR_acceleration_structure" });
	assert(as_exts.empty());

	as_exts.insert("VK_KHR_acceleration_structure");
	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = 256;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
	check_vkCreateBuffer(VK_NULL_HANDLE, &buffer_info, nullptr, nullptr);
	assert(f->has_VK_KHR_acceleration_structure == true);
	assert_removed_device_extensions(f, as_exts, {});
	assert(as_exts.size() == 1);
}

static void test_ray_query_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> rq_exts = { "VK_KHR_ray_query" };
	assert(rq_exts.size() == 1);
	assert_removed_device_extensions(f, rq_exts, { "VK_KHR_ray_query" });
	assert(rq_exts.empty());

	rq_exts.insert("VK_KHR_ray_query");
	VkPhysicalDeviceRayQueryFeaturesKHR rq_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr, VK_TRUE };
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &rq_features };
	const char* rq_extname = "VK_KHR_ray_query";
	dci.ppEnabledExtensionNames = &rq_extname;
	dci.enabledExtensionCount = 1;
	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_KHR_ray_query == true);
	assert_removed_device_extensions(f, rq_exts, {});
	assert(rq_exts.size() == 1);
}

static void test_ray_query_acceleration_structure_dependency()
{
	feature_detection* f = reset_detection();

	check_shader_module_code((const uint32_t*)vulkan_rayquery_frag_spv,
	                         long(ceil(vulkan_rayquery_frag_spv_len / 4.0)) * sizeof(uint32_t),
	                         8);

	std::unordered_set<std::string> rq_exts = { "VK_KHR_acceleration_structure", "VK_KHR_ray_query" };
	assert_removed_device_extensions(f, rq_exts, {});
	assert_string_set_equals(rq_exts, { "VK_KHR_acceleration_structure", "VK_KHR_ray_query" });

	VkPhysicalDeviceRayQueryFeaturesKHR rq_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr };
	rq_features.rayQuery = VK_TRUE;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq_features };
	as_features.accelerationStructure = VK_TRUE;
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &as_features };
	const char* rq_names[] = { "VK_KHR_acceleration_structure", "VK_KHR_ray_query" };
	dci.ppEnabledExtensionNames = rq_names;
	dci.enabledExtensionCount = 2;

	assert_adjusted_device_create_info(f, dci, { "VK_KHR_acceleration_structure", "VK_KHR_ray_query" }, {}, true);
	assert(dci.pNext == &as_features);
	assert(as_features.pNext == &rq_features);
}

static void test_ray_tracing_pipeline_acceleration_structure_dependency()
{
	feature_detection* f = reset_detection();

	check_vkCmdSetRayTracingPipelineStackSizeKHR(VK_NULL_HANDLE, 128);

	std::unordered_set<std::string> rtp_exts = { "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline" };
	assert_removed_device_extensions(f, rtp_exts, {});
	assert_string_set_equals(rtp_exts, { "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline" });

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtp_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr };
	rtp_features.rayTracingPipeline = VK_TRUE;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rtp_features };
	as_features.accelerationStructure = VK_TRUE;
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &as_features };
	const char* rtp_names[] = { "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline" };
	dci.ppEnabledExtensionNames = rtp_names;
	dci.enabledExtensionCount = 2;

	assert_adjusted_device_create_info(f, dci, { "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline" }, {}, true);
	assert(dci.pNext == &as_features);
	assert(as_features.pNext == &rtp_features);
}

static void test_ray_tracing_maintenance1_acceleration_structure_dependency()
{
	feature_detection* f = reset_detection();

	check_vkCmdTraceRaysIndirect2KHR(VK_NULL_HANDLE, 0);

	std::unordered_set<std::string> rtm1_exts = { "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_maintenance1" };
	assert_removed_device_extensions(f, rtm1_exts, {});
	assert_string_set_equals(rtm1_exts, { "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_maintenance1" });

	VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rtm1_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, nullptr };
	rtm1_features.rayTracingMaintenance1 = VK_TRUE;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rtm1_features };
	as_features.accelerationStructure = VK_TRUE;
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &as_features };
	const char* rtm1_names[] = { "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_maintenance1" };
	dci.ppEnabledExtensionNames = rtm1_names;
	dci.enabledExtensionCount = 2;

	assert_adjusted_device_create_info(f, dci, { "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_maintenance1" }, {}, true);
	assert(dci.pNext == &as_features);
	assert(as_features.pNext == &rtm1_features);
}

static void test_ray_tracing_maintenance1_detection()
{
	feature_detection* f = reset_detection();

	VkQueryPoolCreateInfo qpci = {
		VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR, 1, 0
	};
	check_vkCreateQueryPool(VK_NULL_HANDLE, &qpci, nullptr, nullptr);
	assert(f->has_VK_KHR_ray_tracing_maintenance1 == true);

	f = reset_detection();

	qpci.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR;
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

	f = reset_detection();

	VkIndirectCommandsLayoutTokenEXT token = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT, nullptr };
	token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT;
	token.offset = 0;
	VkIndirectCommandsLayoutCreateInfoEXT layout_info = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_EXT, nullptr };
	layout_info.indirectStride = sizeof(VkTraceRaysIndirectCommand2KHR);
	layout_info.tokenCount = 1;
	layout_info.pTokens = &token;
	check_vkCreateIndirectCommandsLayoutEXT(VK_NULL_HANDLE, &layout_info, nullptr, nullptr);
	assert(f->has_VK_KHR_ray_tracing_maintenance1 == true);
}

static void test_maintenance1_detection()
{
	feature_detection* f = reset_detection();

	VkImageCreateInfo image_info = {
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0,
		VK_IMAGE_TYPE_3D, VK_FORMAT_R8G8B8A8_UNORM, { 4, 4, 4 },
		1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr,
		VK_IMAGE_LAYOUT_UNDEFINED
	};
	check_vkCreateImage(VK_NULL_HANDLE, &image_info, nullptr, nullptr);
	assert(f->has_VK_KHR_maintenance1 == false);

	f = reset_detection();
	image_info.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT_KHR;
	check_vkCreateImage(VK_NULL_HANDLE, &image_info, nullptr, nullptr);
	assert(f->has_VK_KHR_maintenance1 == true);

	f = reset_detection();
	VkViewport viewport = { 0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f };
	check_vkCmdSetViewport(VK_NULL_HANDLE, 0, 1, &viewport);
	assert(f->has_VK_KHR_maintenance1 == false);

	f = reset_detection();
	viewport.height = -64.0f;
	check_vkCmdSetViewport(VK_NULL_HANDLE, 0, 1, &viewport);
	assert(f->has_VK_KHR_maintenance1 == true);

	f = reset_detection();
	check_vkTrimCommandPoolKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 0);
	assert(f->has_VK_KHR_maintenance1 == true);
}

static void test_ray_tracing_pipeline_detection()
{
	feature_detection* f = reset_detection();

	VkRayTracingPipelineCreateInfoKHR pipeline_info = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR, nullptr };
	check_vkCreateRayTracingPipelinesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, nullptr);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);

	f = reset_detection();

	VkStridedDeviceAddressRegionKHR region{};
	check_vkCmdTraceRaysKHR(VK_NULL_HANDLE, &region, &region, &region, &region, 1, 1, 1);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);

	f = reset_detection();

	check_vkCmdTraceRaysIndirectKHR(VK_NULL_HANDLE, &region, &region, &region, &region, 0);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);

	f = reset_detection();

	check_vkGetRayTracingShaderGroupHandlesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 1, 0, nullptr);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);

	f = reset_detection();

	check_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 1, 0, nullptr);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);

	f = reset_detection();

	check_vkGetRayTracingShaderGroupStackSizeKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, VK_SHADER_GROUP_SHADER_GENERAL_KHR);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);

	f = reset_detection();

	check_vkCmdSetRayTracingPipelineStackSizeKHR(VK_NULL_HANDLE, 128);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);

	f = reset_detection();

	const uint32_t primitive_culling_spirv[] = {
		SpvMagicNumber,
		0x00010000,
		0,
		3,
		0,
		(uint32_t(2) << 16) | SpvOpCapability,
		SpvCapabilityRayTraversalPrimitiveCullingKHR,
		(uint32_t(3) << 16) | SpvOpMemoryModel,
		SpvAddressingModelLogical,
		SpvMemoryModelGLSL450
	};
	check_shader_module_code(primitive_culling_spirv, sizeof(primitive_culling_spirv), 9);
	assert(f->has_VK_KHR_ray_tracing_pipeline == true);
	assert(f->has_VK_KHR_ray_query == true);
}

static void test_opacity_micromap_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_EXT_opacity_micromap" };
	assert(exts.size() == 1);
	assert_removed_device_extensions(f, exts, { "VK_EXT_opacity_micromap" });
	assert(exts.empty());

	exts.insert("VK_EXT_opacity_micromap");
	const char* extname = "VK_EXT_opacity_micromap";
	const char* names[] = { extname };
	VkPhysicalDeviceOpacityMicromapFeaturesEXT micromap_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT, nullptr, VK_TRUE, VK_TRUE, VK_TRUE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &micromap_features };
	dci.ppEnabledExtensionNames = names;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_EXT_opacity_micromap == false);
	assert_removed_device_extensions(f, exts, { "VK_EXT_opacity_micromap" });
	assert(exts.empty());
	assert_adjusted_device_create_info(f, dci, exts, { "VK_EXT_opacity_micromap" }, false);
}

static void test_opacity_micromap_acceleration_structure_dependency()
{
	feature_detection* f = reset_detection();

	check_vkGetMicromapBuildSizesEXT(VK_NULL_HANDLE, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, nullptr, nullptr);

	std::unordered_set<std::string> exts = { "VK_KHR_acceleration_structure", "VK_KHR_synchronization2", "VK_EXT_opacity_micromap" };
	assert_removed_device_extensions(f, exts, {});
	assert_string_set_equals(exts, { "VK_KHR_acceleration_structure", "VK_KHR_synchronization2", "VK_EXT_opacity_micromap" });

	VkPhysicalDeviceOpacityMicromapFeaturesEXT micromap_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT, nullptr };
	micromap_features.micromap = VK_TRUE;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &micromap_features };
	as_features.accelerationStructure = VK_TRUE;
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &as_features };
	const char* ext_names[] = { "VK_KHR_acceleration_structure", "VK_EXT_opacity_micromap" };
	dci.ppEnabledExtensionNames = ext_names;
	dci.enabledExtensionCount = 2;

	assert_adjusted_device_create_info(f, dci, { "VK_KHR_acceleration_structure", "VK_EXT_opacity_micromap" }, {}, true);
	assert(dci.pNext == &as_features);
	assert(as_features.pNext == &micromap_features);
}

static void test_pipeline_opacity_micromap_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_ARM_pipeline_opacity_micromap" };
	assert_removed_device_extensions(f, exts, { "VK_ARM_pipeline_opacity_micromap" });
	assert(exts.empty());

	exts.insert("VK_ARM_pipeline_opacity_micromap");
	const char* names[] = { "VK_ARM_pipeline_opacity_micromap" };
	VkPhysicalDevicePipelineOpacityMicromapFeaturesARM features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_OPACITY_MICROMAP_FEATURES_ARM, nullptr, VK_TRUE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features };
	dci.ppEnabledExtensionNames = names;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_ARM_pipeline_opacity_micromap == false);
	assert_removed_device_extensions(f, exts, { "VK_ARM_pipeline_opacity_micromap" });
	assert(exts.empty());
	assert_adjusted_device_create_info(f, dci, exts, { "VK_ARM_pipeline_opacity_micromap" }, false);
}

static void test_pipeline_opacity_micromap_dependency()
{
	feature_detection* f = reset_detection();

	VkPipelineCreateFlags2CreateInfo flags2 = {
		VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO, nullptr,
		VK_PIPELINE_CREATE_2_DISALLOW_OPACITY_MICROMAP_BIT_ARM
	};
	VkComputePipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, &flags2 };
	check_vkCreateComputePipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, nullptr);
	assert(f->has_VK_ARM_pipeline_opacity_micromap == true);

	std::unordered_set<std::string> exts = {
		"VK_KHR_acceleration_structure",
		"VK_KHR_synchronization2",
		"VK_EXT_opacity_micromap",
		"VK_ARM_pipeline_opacity_micromap"
	};
	assert_removed_device_extensions(f, exts, {});
	assert_string_set_equals(exts, {
		"VK_KHR_acceleration_structure",
		"VK_KHR_synchronization2",
		"VK_EXT_opacity_micromap",
		"VK_ARM_pipeline_opacity_micromap"
	});
}

static void test_opacity_micromap_detection()
{
	feature_detection* f = reset_detection();

	VkQueryPoolCreateInfo qpci = {
		VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_MICROMAP_SERIALIZATION_SIZE_EXT, 1, 0
	};
	check_vkCreateQueryPool(VK_NULL_HANDLE, &qpci, nullptr, nullptr);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = 256;
	buffer_info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
	check_vkCreateBuffer(VK_NULL_HANDLE, &buffer_info, nullptr, nullptr);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkMicromapUsageEXT usage = { 1, 0, VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT };
	VkMicromapTriangleEXT triangle = { 0, 0, VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT };
	VkMicromapBuildInfoEXT build_info = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT, nullptr };
	build_info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
	build_info.flags = VK_BUILD_MICROMAP_ALLOW_COMPACTION_BIT_EXT;
	build_info.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
	build_info.usageCountsCount = 1;
	build_info.pUsageCounts = &usage;
	build_info.triangleArray.hostAddress = &triangle;
	VkMicromapBuildSizesInfoEXT micromap_sizes = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT, nullptr };
	check_vkGetMicromapBuildSizesEXT(VK_NULL_HANDLE, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR, &build_info, &micromap_sizes);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkMicromapCreateInfoEXT create_info = { VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT, nullptr };
	create_info.buffer = VK_NULL_HANDLE;
	create_info.size = 256;
	create_info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
	check_vkCreateMicromapEXT(VK_NULL_HANDLE, &create_info, nullptr, nullptr);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkDestroyMicromapEXT(VK_NULL_HANDLE, (VkMicromapEXT)1, nullptr);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkBuildMicromapsEXT(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &build_info);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkCmdBuildMicromapsEXT(VK_NULL_HANDLE, 1, &build_info);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkCopyMicromapInfoEXT copy_info = { VK_STRUCTURE_TYPE_COPY_MICROMAP_INFO_EXT, nullptr, (VkMicromapEXT)1, (VkMicromapEXT)2, VK_COPY_MICROMAP_MODE_COMPACT_EXT };
	check_vkCopyMicromapEXT(VK_NULL_HANDLE, VK_NULL_HANDLE, &copy_info);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkCmdCopyMicromapEXT(VK_NULL_HANDLE, &copy_info);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkCopyMicromapToMemoryInfoEXT copy_to_memory_info = { VK_STRUCTURE_TYPE_COPY_MICROMAP_TO_MEMORY_INFO_EXT, nullptr, (VkMicromapEXT)1, {}, VK_COPY_MICROMAP_MODE_SERIALIZE_EXT };
	check_vkCopyMicromapToMemoryEXT(VK_NULL_HANDLE, VK_NULL_HANDLE, &copy_to_memory_info);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkCmdCopyMicromapToMemoryEXT(VK_NULL_HANDLE, &copy_to_memory_info);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkCopyMemoryToMicromapInfoEXT copy_from_memory_info = { VK_STRUCTURE_TYPE_COPY_MEMORY_TO_MICROMAP_INFO_EXT, nullptr, {}, (VkMicromapEXT)1, VK_COPY_MICROMAP_MODE_DESERIALIZE_EXT };
	check_vkCopyMemoryToMicromapEXT(VK_NULL_HANDLE, VK_NULL_HANDLE, &copy_from_memory_info);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkCmdCopyMemoryToMicromapEXT(VK_NULL_HANDLE, &copy_from_memory_info);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkWriteMicromapsPropertiesEXT(VK_NULL_HANDLE, 1, nullptr, VK_QUERY_TYPE_MICROMAP_COMPACTED_SIZE_EXT, sizeof(VkDeviceSize), nullptr, sizeof(VkDeviceSize));
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkCmdWriteMicromapsPropertiesEXT(VK_NULL_HANDLE, 1, nullptr, VK_QUERY_TYPE_MICROMAP_SERIALIZATION_SIZE_EXT, VK_NULL_HANDLE, 0);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	uint8_t version_data[16] = {};
	VkMicromapVersionInfoEXT version_info = { VK_STRUCTURE_TYPE_MICROMAP_VERSION_INFO_EXT, nullptr, version_data };
	VkAccelerationStructureCompatibilityKHR compatibility = VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR;
	check_vkGetDeviceMicromapCompatibilityEXT(VK_NULL_HANDLE, &version_info, &compatibility);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkAccelerationStructureTrianglesOpacityMicromapEXT opacity_info = {
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT, nullptr,
		VK_INDEX_TYPE_NONE_KHR, {}, 0, 0, 1, &usage, nullptr, (VkMicromapEXT)1
	};
	VkAccelerationStructureGeometryTrianglesDataKHR triangles = {
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR, &opacity_info
	};
	VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr };
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles = triangles;
	VkAccelerationStructureBuildGeometryInfoKHR as_build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr };
	as_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	as_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_BIT_EXT;
	as_build_info.geometryCount = 1;
	as_build_info.pGeometries = &geometry;
	uint32_t primitive_count = 1;
	VkAccelerationStructureBuildSizesInfoKHR as_sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr };
	check_vkGetAccelerationStructureBuildSizesKHR(VK_NULL_HANDLE, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &as_build_info, &primitive_count, &as_sizes);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkBuildAccelerationStructuresKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &as_build_info, nullptr);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();
	check_vkCmdBuildAccelerationStructuresKHR(VK_NULL_HANDLE, 1, &as_build_info, nullptr);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkMemoryBarrier2 memory_barrier = {
		VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr,
		VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT,
		VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT,
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
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkRayTracingPipelineCreateInfoKHR pipeline_info = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR, nullptr };
	pipeline_info.flags = VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;
	check_vkCreateRayTracingPipelinesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, nullptr);
	assert(f->has_VK_EXT_opacity_micromap == true);

	f = reset_detection();

	VkPipelineCreateFlags2CreateInfo flags2 = {
		VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO, nullptr,
		VK_PIPELINE_CREATE_2_DISALLOW_OPACITY_MICROMAP_BIT_ARM
	};
	VkGraphicsPipelineCreateInfo graphics_pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &flags2 };
	check_vkCreateGraphicsPipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr, nullptr);
	assert(f->has_VK_ARM_pipeline_opacity_micromap == true);

	f = reset_detection();

	VkComputePipelineCreateInfo compute_pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, &flags2 };
	check_vkCreateComputePipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &compute_pipeline_info, nullptr, nullptr);
	assert(f->has_VK_ARM_pipeline_opacity_micromap == true);

	f = reset_detection();

	pipeline_info.flags = 0;
	pipeline_info.pNext = &flags2;
	check_vkCreateRayTracingPipelinesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, nullptr);
	assert(f->has_VK_ARM_pipeline_opacity_micromap == true);

	f = reset_detection();

	buffer_info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
	std::unordered_set<std::string> exts = { "VK_EXT_opacity_micromap" };
	check_vkCreateBuffer(VK_NULL_HANDLE, &buffer_info, nullptr, nullptr);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
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

	f = reset_detection();

	const uint32_t opacity_micromap_spirv[] = {
		SpvMagicNumber,
		0x00010000,
		0,
		3,
		0,
		(uint32_t(2) << 16) | SpvOpCapability,
		SpvCapabilityRayTracingOpacityMicromapEXT,
		(uint32_t(3) << 16) | SpvOpMemoryModel,
		SpvAddressingModelLogical,
		SpvMemoryModelGLSL450
	};
	check_shader_module_code(opacity_micromap_spirv, sizeof(opacity_micromap_spirv), 3);
	assert(f->has_VK_EXT_opacity_micromap == true);
}

static void test_tensor_extension_adjustment()
{
	feature_detection* f = reset_detection();

	std::unordered_set<std::string> exts = { "VK_ARM_tensors" };
	assert(exts.size() == 1);
	assert(f->has_VK_ARM_tensors == false);
	assert_removed_device_extensions(f, exts, { "VK_ARM_tensors" });
	assert(exts.empty());

	const char* extname = "VK_ARM_tensors";
	const char* names[] = { extname };
	VkPhysicalDeviceDescriptorBufferTensorFeaturesARM descriptor_buffer_tensor_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_FEATURES_ARM, nullptr, VK_FALSE
	};
	VkPhysicalDeviceTensorFeaturesARM tensor_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM, &descriptor_buffer_tensor_features,
		VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE
	};
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &tensor_features };
	dci.ppEnabledExtensionNames = names;
	dci.enabledExtensionCount = 1;

	check_vkCreateDevice(VK_NULL_HANDLE, &dci, nullptr, nullptr);
	assert(f->has_VK_ARM_tensors == true);
	exts.insert("VK_ARM_tensors");
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
	assert_adjusted_device_create_info(f, dci, exts, {}, true);

	f = reset_detection();
	exts = { "VK_ARM_tensors" };
	descriptor_buffer_tensor_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_FEATURES_ARM, nullptr, VK_FALSE
	};
	tensor_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM, &descriptor_buffer_tensor_features,
		VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE
	};
	dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &tensor_features };
	dci.ppEnabledExtensionNames = names;
	dci.enabledExtensionCount = 1;
	assert_removed_device_extensions(f, exts, { "VK_ARM_tensors" });
	assert(exts.empty());
	assert_adjusted_device_create_info(f, dci, exts, { "VK_ARM_tensors" }, false);

	f = reset_detection();
	VkExternalMemoryTensorCreateInfoARM external_tensor_info = {
		VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_TENSOR_CREATE_INFO_ARM, nullptr, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
	};
	VkTensorDescriptionARM tensor_description = { VK_STRUCTURE_TYPE_TENSOR_DESCRIPTION_ARM, nullptr };
	VkTensorCreateInfoARM tensor_create_info = { VK_STRUCTURE_TYPE_TENSOR_CREATE_INFO_ARM, &external_tensor_info };
	tensor_create_info.pDescription = &tensor_description;
	check_vkCreateTensorARM(VK_NULL_HANDLE, &tensor_create_info, nullptr, nullptr);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkTensorViewCreateInfoARM tensor_view_create_info = { VK_STRUCTURE_TYPE_TENSOR_VIEW_CREATE_INFO_ARM, nullptr };
	check_vkCreateTensorViewARM(VK_NULL_HANDLE, &tensor_view_create_info, nullptr, nullptr);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkTensorMemoryRequirementsInfoARM tensor_memory_requirements_info = {
		VK_STRUCTURE_TYPE_TENSOR_MEMORY_REQUIREMENTS_INFO_ARM, nullptr, VK_NULL_HANDLE
	};
	VkMemoryRequirements2 memory_requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
	check_vkGetTensorMemoryRequirementsARM(VK_NULL_HANDLE, &tensor_memory_requirements_info, &memory_requirements);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkBindTensorMemoryInfoARM bind_tensor_info = {
		VK_STRUCTURE_TYPE_BIND_TENSOR_MEMORY_INFO_ARM, nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, 0
	};
	VkResult result = check_vkBindTensorMemoryARM(VK_NULL_HANDLE, 1, &bind_tensor_info);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkDeviceTensorMemoryRequirementsARM device_tensor_memory_requirements = {
		VK_STRUCTURE_TYPE_DEVICE_TENSOR_MEMORY_REQUIREMENTS_ARM, nullptr, &tensor_create_info
	};
	check_vkGetDeviceTensorMemoryRequirementsARM(VK_NULL_HANDLE, &device_tensor_memory_requirements, &memory_requirements);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkTensorCopyARM tensor_copy_region = { VK_STRUCTURE_TYPE_TENSOR_COPY_ARM, nullptr, 0, nullptr, nullptr, nullptr };
	VkCopyTensorInfoARM copy_tensor_info = {
		VK_STRUCTURE_TYPE_COPY_TENSOR_INFO_ARM, nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &tensor_copy_region
	};
	check_vkCmdCopyTensorARM(VK_NULL_HANDLE, &copy_tensor_info);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkPhysicalDeviceExternalTensorInfoARM external_tensor_properties_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_TENSOR_INFO_ARM, nullptr, 0, &tensor_description,
		VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
	};
	VkExternalTensorPropertiesARM external_tensor_properties = {
		VK_STRUCTURE_TYPE_EXTERNAL_TENSOR_PROPERTIES_ARM, nullptr
	};
	check_vkGetPhysicalDeviceExternalTensorPropertiesARM(
		VK_NULL_HANDLE, &external_tensor_properties_info, &external_tensor_properties);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkTensorCaptureDescriptorDataInfoARM tensor_capture_info = {
		VK_STRUCTURE_TYPE_TENSOR_CAPTURE_DESCRIPTOR_DATA_INFO_ARM, nullptr, VK_NULL_HANDLE
	};
	result = check_vkGetTensorOpaqueCaptureDescriptorDataARM(VK_NULL_HANDLE, &tensor_capture_info, nullptr);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkTensorViewCaptureDescriptorDataInfoARM tensor_view_capture_info = {
		VK_STRUCTURE_TYPE_TENSOR_VIEW_CAPTURE_DESCRIPTOR_DATA_INFO_ARM, nullptr, VK_NULL_HANDLE
	};
	result = check_vkGetTensorViewOpaqueCaptureDescriptorDataARM(VK_NULL_HANDLE, &tensor_view_capture_info, nullptr);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkWriteDescriptorSetTensorARM write_tensor = {
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_TENSOR_ARM, nullptr, 0, nullptr
	};
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &write_tensor };
	write.descriptorType = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
	check_vkUpdateDescriptorSets(VK_NULL_HANDLE, 1, &write, 0, nullptr);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkDescriptorGetTensorInfoARM descriptor_get_tensor_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_GET_TENSOR_INFO_ARM, nullptr, VK_NULL_HANDLE
	};
	VkDescriptorGetInfoEXT descriptor_get_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT, &descriptor_get_tensor_info
	};
	descriptor_get_info.type = VK_DESCRIPTOR_TYPE_TENSOR_ARM;
	check_vkGetDescriptorEXT(VK_NULL_HANDLE, &descriptor_get_info, 0, nullptr);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkMemoryDedicatedAllocateInfoTensorARM dedicated_tensor_allocate = {
		VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_TENSOR_ARM, nullptr, VK_NULL_HANDLE
	};
	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &dedicated_tensor_allocate };
	result = check_vkAllocateMemory(VK_NULL_HANDLE, &allocate_info, nullptr, nullptr);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkTensorMemoryBarrierARM tensor_barrier = {
		VK_STRUCTURE_TYPE_TENSOR_MEMORY_BARRIER_ARM, nullptr, 0, 0, 0, 0, 0, 0, VK_NULL_HANDLE
	};
	VkTensorDependencyInfoARM tensor_dependency = {
		VK_STRUCTURE_TYPE_TENSOR_DEPENDENCY_INFO_ARM, nullptr, 1, &tensor_barrier
	};
	VkDependencyInfo dependency_info = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, &tensor_dependency };
	check_vkCmdPipelineBarrier2(VK_NULL_HANDLE, &dependency_info);
	assert(f->has_VK_ARM_tensors == true);

	VkTensorARM tensors[] = { VK_NULL_HANDLE };
	VkFrameBoundaryTensorsARM frame_boundary_tensors = {
		VK_STRUCTURE_TYPE_FRAME_BOUNDARY_TENSORS_ARM, nullptr, 1, tensors
	};
	VkFrameBoundaryEXT frame_boundary = { VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT, &frame_boundary_tensors };

	f->has_VK_ARM_tensors.store(false);
	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &frame_boundary };
	result = check_vkQueueSubmit(VK_NULL_HANDLE, 1, &submit_info, VK_NULL_HANDLE);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkSubmitInfo2 submit_info2 = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2, &frame_boundary };
	result = check_vkQueueSubmit2(VK_NULL_HANDLE, 1, &submit_info2, VK_NULL_HANDLE);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkBindSparseInfo bind_sparse_info = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO, &frame_boundary };
	result = check_vkQueueBindSparse(VK_NULL_HANDLE, 1, &bind_sparse_info, VK_NULL_HANDLE);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_ARM_tensors == true);

	f->has_VK_ARM_tensors.store(false);
	VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, &frame_boundary };
	result = check_vkQueuePresentKHR(VK_NULL_HANDLE, &present_info);
	assert(result == VK_SUCCESS);
	assert(f->has_VK_ARM_tensors == true);
}

static void test_tensor_spirv_detection()
{
	feature_detection* f = reset_detection();
	std::unordered_set<std::string> exts = { "VK_ARM_tensors" };

	const uint32_t tensor_spirv[] = {
		SpvMagicNumber,
		0x00010000,
		0,
		3,
		0,
		(uint32_t(2) << 16) | SpvOpCapability,
		4174,
		(uint32_t(2) << 16) | SpvOpCapability,
		4175,
		(uint32_t(2) << 16) | SpvOpCapability,
		4176,
		(uint32_t(3) << 16) | SpvOpMemoryModel,
		SpvAddressingModelLogical,
		SpvMemoryModelGLSL450
	};
	check_shader_module_code(tensor_spirv, sizeof(tensor_spirv), 7);
	assert(f->has_VK_ARM_tensors == true);
	assert_removed_device_extensions(f, exts, {});
	assert(exts.size() == 1);
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

static void test_transform_feedback_shader_module()
{
	reset_detection();
	check_shader_module_code((const uint32_t*)vulkan_transform_feedback_vert_spirv,
	                         long(ceil(vulkan_transform_feedback_vert_spirv_len / 4.0)) * sizeof(uint32_t),
	                         6);
	assert(vulkan_feature_detection_get()->has_VK_EXT_transform_feedback == true);
}

static void test_ray_query_shader_module()
{
	reset_detection();
	check_shader_module_code((const uint32_t*)vulkan_rayquery_frag_spv,
	                         long(ceil(vulkan_rayquery_frag_spv_len / 4.0)) * sizeof(uint32_t),
	                         8);
	assert(vulkan_feature_detection_get()->has_VK_KHR_ray_query == true);
}

int main()
{
	test_logic_op_adjustment();
	test_texture_compression_adjustment();
	test_vulkan12_adjustment();
	test_extension_chain_helpers();
	test_shader_atomic_int64_extension_adjustment();
	test_bind_memory2_extension_adjustment();
	test_copy_commands2_extension_adjustment();
	test_create_renderpass2_extension_adjustment();
	test_dynamic_rendering_extension_adjustment();
	test_synchronization2_extension_adjustment();
	test_transform_feedback_extension_adjustment();
	test_descriptor_indexing_extension_adjustment();
	test_get_physical_device_properties2_extension_adjustment();
	test_external_fence_capabilities_extension_adjustment();
	test_get_memory_requirements2_extension_adjustment();
	test_map_memory2_extension_adjustment();
	test_external_memory_extension_adjustment();
	test_external_memory_fd_extension_adjustment();
	test_external_memory_host_extension_adjustment();
	test_robustness2_extension_adjustment();
	test_multiview_extension_adjustment();
	test_multiview_render_pass_detection();
	test_multiview_render_pass2_detection();
	test_multiview_dynamic_rendering_detection();
	test_maintenance1_extension_adjustment();
	test_ray_tracing_pipeline_extension_adjustment();
	test_ray_tracing_maintenance1_extension_adjustment();
	test_acceleration_structure_extension_adjustment();
	test_ray_query_extension_adjustment();
	test_ray_query_acceleration_structure_dependency();
	test_ray_tracing_pipeline_acceleration_structure_dependency();
	test_ray_tracing_maintenance1_acceleration_structure_dependency();
	test_opacity_micromap_extension_adjustment();
	test_opacity_micromap_acceleration_structure_dependency();
	test_pipeline_opacity_micromap_extension_adjustment();
	test_pipeline_opacity_micromap_dependency();
	test_maintenance1_detection();
	test_opacity_micromap_detection();
	test_ray_tracing_pipeline_detection();
	test_ray_tracing_maintenance1_detection();
	test_spirv_extension_detection();
	test_tensor_extension_adjustment();
	test_tensor_spirv_detection();
	test_vulkan11_multiview_adjustment();
	test_vulkan11_multiview_shader_adjustment();
	test_large_points_detection();
	test_vulkan14_line_rasterization_adjustment();
	test_buffer_device_address_shader_module();
	test_transform_feedback_shader_module();
	test_ray_query_shader_module();
	return 0;
}
