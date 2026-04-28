#include "vulkan_common.h"
#include "external/stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct optical_flow_instance_functions
{
	PFN_vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM get_queue_family_data_graph_properties = nullptr;
	PFN_vkGetPhysicalDeviceQueueFamilyDataGraphEngineOperationPropertiesARM get_engine_operation_properties = nullptr;
	PFN_vkGetPhysicalDeviceQueueFamilyDataGraphOpticalFlowImageFormatsARM get_optical_flow_image_formats = nullptr;
};

struct optical_flow_device_functions
{
	PFN_vkCreateDataGraphPipelinesARM create_pipelines = nullptr;
	PFN_vkCreateDataGraphPipelineSessionARM create_session = nullptr;
	PFN_vkGetDataGraphPipelineSessionBindPointRequirementsARM get_bind_point_requirements = nullptr;
	PFN_vkGetDataGraphPipelineSessionMemoryRequirementsARM get_memory_requirements = nullptr;
	PFN_vkBindDataGraphPipelineSessionMemoryARM bind_session_memory = nullptr;
	PFN_vkDestroyDataGraphPipelineSessionARM destroy_session = nullptr;
	PFN_vkCmdDispatchDataGraphARM cmd_dispatch = nullptr;
};

struct image_resource
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
};

struct buffer_resource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct data_graph_session
{
	VkDataGraphPipelineSessionARM session = VK_NULL_HANDLE;
	std::vector<VkDeviceMemory> memories;
};

static uint32_t choose_grid_granularity(VkDataGraphOpticalFlowGridSizeFlagsARM supported)
{
	if ((supported & VK_DATA_GRAPH_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_ARM) != 0) return 1;
	if ((supported & VK_DATA_GRAPH_OPTICAL_FLOW_GRID_SIZE_2X2_BIT_ARM) != 0) return 2;
	if ((supported & VK_DATA_GRAPH_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_ARM) != 0) return 4;
	if ((supported & VK_DATA_GRAPH_OPTICAL_FLOW_GRID_SIZE_8X8_BIT_ARM) != 0) return 8;
	return 0;
}

static VkDataGraphOpticalFlowGridSizeFlagsARM grid_flag_from_granularity(uint32_t granularity)
{
	switch (granularity)
	{
	case 1: return VK_DATA_GRAPH_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_ARM;
	case 2: return VK_DATA_GRAPH_OPTICAL_FLOW_GRID_SIZE_2X2_BIT_ARM;
	case 4: return VK_DATA_GRAPH_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_ARM;
	case 8: return VK_DATA_GRAPH_OPTICAL_FLOW_GRID_SIZE_8X8_BIT_ARM;
	default: break;
	}
	assert(false);
	return 0;
}

static uint32_t round_up_to_multiple(uint32_t value, uint32_t multiple)
{
	assert(multiple > 0);
	const uint32_t remainder = value % multiple;
	return remainder == 0 ? value : (value + multiple - remainder);
}

static VkFormat choose_required_format(const std::vector<VkFormat>& formats, VkFormat preferred)
{
	if (std::find(formats.begin(), formats.end(), preferred) != formats.end()) return preferred;
	return VK_FORMAT_UNDEFINED;
}

static bool contains_format(const std::vector<VkFormat>& formats, VkFormat format)
{
	return std::find(formats.begin(), formats.end(), format) != formats.end();
}

static std::vector<VkFormat> query_optical_flow_formats(const vulkan_setup_t& vulkan, const optical_flow_instance_functions& f,
                                                        const VkQueueFamilyDataGraphPropertiesARM& operation, VkDataGraphOpticalFlowImageUsageFlagsARM usage)
{
	VkDataGraphOpticalFlowImageFormatInfoARM info = { VK_STRUCTURE_TYPE_DATA_GRAPH_OPTICAL_FLOW_IMAGE_FORMAT_INFO_ARM, nullptr };
	info.usage = usage;

	uint32_t format_count = 0;
	VkResult result = f.get_optical_flow_image_formats(vulkan.physical, vulkan.queue_family_index, &operation, &info, &format_count, nullptr);
	check(result);
	assert(format_count > 0);

	std::vector<VkDataGraphOpticalFlowImageFormatPropertiesARM> properties(format_count);
	for (auto& property : properties)
	{
		property = { VK_STRUCTURE_TYPE_DATA_GRAPH_OPTICAL_FLOW_IMAGE_FORMAT_PROPERTIES_ARM, nullptr };
	}

	result = f.get_optical_flow_image_formats(vulkan.physical, vulkan.queue_family_index, &operation, &info, &format_count, properties.data());
	check(result);
	assert(format_count == properties.size());

	std::vector<VkFormat> formats;
	formats.reserve(format_count);
	for (const auto& property : properties)
	{
		formats.push_back(property.format);
	}
	return formats;
}

static data_graph_session create_data_graph_session(const vulkan_setup_t& vulkan, const optical_flow_device_functions& f, VkPipeline pipeline)
{
	data_graph_session session;
	VkDataGraphPipelineSessionCreateInfoARM create_info = { VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SESSION_CREATE_INFO_ARM, nullptr };
	create_info.flags = VK_DATA_GRAPH_PIPELINE_SESSION_CREATE_OPTICAL_FLOW_CACHE_BIT_ARM;
	create_info.dataGraphPipeline = pipeline;
	VkResult result = f.create_session(vulkan.device, &create_info, nullptr, &session.session);
	check(result);

	VkDataGraphPipelineSessionBindPointRequirementsInfoARM bind_req_info = {
		VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_REQUIREMENTS_INFO_ARM, nullptr };
	bind_req_info.session = session.session;

	uint32_t requirement_count = 0;
	result = f.get_bind_point_requirements(vulkan.device, &bind_req_info, &requirement_count, nullptr);
	check(result);
	assert(requirement_count > 0);

	std::vector<VkDataGraphPipelineSessionBindPointRequirementARM> requirements(requirement_count);
	for (auto& requirement : requirements)
	{
		requirement = { VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_REQUIREMENT_ARM, nullptr };
	}

	result = f.get_bind_point_requirements(vulkan.device, &bind_req_info, &requirement_count, requirements.data());
	check(result);
	assert(requirement_count == requirements.size());

	for (const auto& requirement : requirements)
	{
		assert(requirement.bindPointType == VK_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_TYPE_MEMORY_ARM);
		assert(requirement.numObjects > 0);

		for (uint32_t object_index = 0; object_index < requirement.numObjects; ++object_index)
		{
			VkMemoryRequirements2 memory_requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
			VkDataGraphPipelineSessionMemoryRequirementsInfoARM memory_info = {
				VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SESSION_MEMORY_REQUIREMENTS_INFO_ARM, nullptr };
			memory_info.session = session.session;
			memory_info.bindPoint = requirement.bindPoint;
			memory_info.objectIndex = object_index;
			f.get_memory_requirements(vulkan.device, &memory_info, &memory_requirements);
			assert(memory_requirements.memoryRequirements.size > 0);

			VkMemoryAllocateInfo allocation_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
			allocation_info.allocationSize = memory_requirements.memoryRequirements.size;
			allocation_info.memoryTypeIndex = get_device_memory_type(
				memory_requirements.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkDeviceMemory memory = VK_NULL_HANDLE;
			result = vkAllocateMemory(vulkan.device, &allocation_info, nullptr, &memory);
			check(result);

			VkBindDataGraphPipelineSessionMemoryInfoARM bind_info = {
				VK_STRUCTURE_TYPE_BIND_DATA_GRAPH_PIPELINE_SESSION_MEMORY_INFO_ARM, nullptr };
			bind_info.session = session.session;
			bind_info.bindPoint = requirement.bindPoint;
			bind_info.objectIndex = object_index;
			bind_info.memory = memory;
			bind_info.memoryOffset = 0;
			result = f.bind_session_memory(vulkan.device, 1, &bind_info);
			check(result);

			session.memories.push_back(memory);
		}
	}

	return session;
}

static buffer_resource create_buffer_resource(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage,
                                              VkMemoryPropertyFlags properties, const char* name)
{
	buffer_resource resource;

	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &resource.buffer);
	check(result);
	assert(resource.buffer != VK_NULL_HANDLE);
	if (name) test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resource.buffer, name);

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, resource.buffer, &requirements);

	VkMemoryAllocateInfo allocation_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocation_info.allocationSize = requirements.size;
	allocation_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, properties);

	result = vkAllocateMemory(vulkan.device, &allocation_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, resource.buffer, resource.memory, 0);
	check(result);
	return resource;
}

static void write_buffer_resource(const vulkan_setup_t& vulkan, const buffer_resource& resource, const std::vector<uint8_t>& data)
{
	void* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, resource.memory, 0, data.size(), 0, &mapped);
	check(result);
	assert(mapped != nullptr);
	std::memcpy(mapped, data.data(), data.size());
	vkUnmapMemory(vulkan.device, resource.memory);
}

static void destroy_buffer_resource(const vulkan_setup_t& vulkan, buffer_resource& resource)
{
	if (resource.buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan.device, resource.buffer, nullptr);
		resource.buffer = VK_NULL_HANDLE;
	}
	if (resource.memory != VK_NULL_HANDLE)
	{
		testFreeMemory(vulkan, resource.memory);
		resource.memory = VK_NULL_HANDLE;
	}
}

static void destroy_data_graph_session(const vulkan_setup_t& vulkan, const optical_flow_device_functions& f, data_graph_session& session)
{
	if (session.session != VK_NULL_HANDLE)
	{
		f.destroy_session(vulkan.device, session.session, nullptr);
		session.session = VK_NULL_HANDLE;
	}

	for (VkDeviceMemory memory : session.memories)
	{
		if (memory != VK_NULL_HANDLE) testFreeMemory(vulkan, memory);
	}
	session.memories.clear();
}

static image_resource create_image_resource(const vulkan_setup_t& vulkan, VkFormat format, uint32_t width, uint32_t height,
                                            VkImageUsageFlags usage, const char* name)
{
	image_resource resource;

	VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = format;
	image_info.extent = { width, height, 1 };
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = usage;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage(vulkan.device, &image_info, nullptr, &resource.image);
	check(result);
	assert(resource.image != VK_NULL_HANDLE);
	if (name) test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)resource.image, name);

	VkMemoryRequirements memory_requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, resource.image, &memory_requirements);

	VkMemoryAllocateInfo allocation_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocation_info.allocationSize = memory_requirements.size;
	allocation_info.memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(vulkan.device, &allocation_info, nullptr, &resource.memory);
	check(result);
	result = vkBindImageMemory(vulkan.device, resource.image, resource.memory, 0);
	check(result);

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
	view_info.image = resource.image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	result = vkCreateImageView(vulkan.device, &view_info, nullptr, &resource.view);
	check(result);
	assert(resource.view != VK_NULL_HANDLE);
	if (name) test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)resource.view, name);

	return resource;
}

static void destroy_image_resource(const vulkan_setup_t& vulkan, image_resource& resource)
{
	if (resource.view != VK_NULL_HANDLE)
	{
		vkDestroyImageView(vulkan.device, resource.view, nullptr);
		resource.view = VK_NULL_HANDLE;
	}
	if (resource.image != VK_NULL_HANDLE)
	{
		vkDestroyImage(vulkan.device, resource.image, nullptr);
		resource.image = VK_NULL_HANDLE;
	}
	if (resource.memory != VK_NULL_HANDLE)
	{
		testFreeMemory(vulkan, resource.memory);
		resource.memory = VK_NULL_HANDLE;
	}
}

static void add_general_image_barrier(std::vector<VkImageMemoryBarrier2>& barriers, VkImage image, VkPipelineStageFlags2 src_stage,
                                      VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                                      VkImageLayout old_layout, VkImageLayout new_layout)
{
	VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr };
	barrier.srcStageMask = src_stage;
	barrier.srcAccessMask = src_access;
	barrier.dstStageMask = dst_stage;
	barrier.dstAccessMask = dst_access;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barriers.push_back(barrier);
}

static void cmd_pipeline_barrier2(VkCommandBuffer command_buffer, const std::vector<VkImageMemoryBarrier2>& barriers)
{
	assert(!barriers.empty());
	VkDependencyInfo dependency_info = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr };
	dependency_info.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
	dependency_info.pImageMemoryBarriers = barriers.data();
	vkCmdPipelineBarrier2(command_buffer, &dependency_info);
}

static int skip_not_supported(vulkan_setup_t& vulkan, const char* reason)
{
	printf("%s\n", reason);
	test_done(vulkan);
	return 77;
}

static bool using_graph_emulation_layer()
{
	const char* layers = getenv("VK_INSTANCE_LAYERS");
	return layers != nullptr && strstr(layers, "VK_LAYER_ML_Graph_Emulation") != nullptr;
}

static void show_usage()
{
	printf("-i/--image-output      Save input and flow-visualization PNGs to disk\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)argc;
	if (match(argv[i], "-i", "--image-output"))
	{
		reqs.options["image_output"] = true;
		return true;
	}
	return false;
}

static std::vector<uint8_t> make_square_frame(uint32_t width, uint32_t height, uint32_t square_x, uint32_t square_y,
                                              uint32_t square_width, uint32_t square_height)
{
	std::vector<uint8_t> image(width * height, 0);
	for (uint32_t y = 0; y < square_height; ++y)
	{
		for (uint32_t x = 0; x < square_width; ++x)
		{
			image[(square_y + y) * width + square_x + x] = 255;
		}
	}
	return image;
}

static float half_to_float(uint16_t value)
{
	const uint32_t sign = uint32_t(value & 0x8000u) << 16;
	const uint32_t exponent = (value >> 10) & 0x1fu;
	const uint32_t mantissa = value & 0x03ffu;

	uint32_t bits = 0;
	if (exponent == 0)
	{
		if (mantissa == 0)
		{
			bits = sign;
		}
		else
		{
			int32_t normalized_exponent = -14;
			uint32_t normalized_mantissa = mantissa;
			while ((normalized_mantissa & 0x0400u) == 0)
			{
				normalized_mantissa <<= 1;
				normalized_exponent--;
			}
			normalized_mantissa &= 0x03ffu;
			bits = sign | (uint32_t(normalized_exponent + 127) << 23) | (normalized_mantissa << 13);
		}
	}
	else if (exponent == 0x1fu)
	{
		bits = sign | 0x7f800000u | (mantissa << 13);
	}
	else
	{
		bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
	}

	float out = 0.0f;
	std::memcpy(&out, &bits, sizeof(out));
	return out;
}

static uint8_t clamp_to_u8(float value)
{
	if (value <= 0.0f) return 0;
	if (value >= 255.0f) return 255;
	return static_cast<uint8_t>(value);
}

static void save_r8_image_png(const char* filename, const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height)
{
	assert(pixels.size() == size_t(width) * size_t(height));
	std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4);
	for (size_t i = 0; i < pixels.size(); ++i)
	{
		rgba[i * 4 + 0] = pixels[i];
		rgba[i * 4 + 1] = pixels[i];
		rgba[i * 4 + 2] = pixels[i];
		rgba[i * 4 + 3] = 255;
	}
	const int result = stbi_write_png(filename, int(width), int(height), 4, rgba.data(), 0);
	assert(result != 0);
}

static void save_flow_visualization_png(const char* filename, const std::vector<uint8_t>& flow_bytes, uint32_t width, uint32_t height)
{
	assert(flow_bytes.size() == size_t(width) * size_t(height) * sizeof(uint16_t) * 2);
	std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4);
	float max_magnitude = 0.0f;
	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			const size_t offset = (size_t(y) * width + x) * sizeof(uint16_t) * 2;
			uint16_t raw_x = 0;
			uint16_t raw_y = 0;
			std::memcpy(&raw_x, flow_bytes.data() + offset, sizeof(raw_x));
			std::memcpy(&raw_y, flow_bytes.data() + offset + sizeof(raw_x), sizeof(raw_y));
			const float flow_x = half_to_float(raw_x);
			const float flow_y = half_to_float(raw_y);
			max_magnitude = std::max(max_magnitude, std::sqrt(flow_x * flow_x + flow_y * flow_y));
		}
	}
	if (max_magnitude < 0.001f) max_magnitude = 1.0f;

	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			const size_t flow_offset = (size_t(y) * width + x) * sizeof(uint16_t) * 2;
			uint16_t raw_x = 0;
			uint16_t raw_y = 0;
			std::memcpy(&raw_x, flow_bytes.data() + flow_offset, sizeof(raw_x));
			std::memcpy(&raw_y, flow_bytes.data() + flow_offset + sizeof(raw_x), sizeof(raw_y));
			const float flow_x = half_to_float(raw_x);
			const float flow_y = half_to_float(raw_y);
			const float magnitude = std::sqrt(flow_x * flow_x + flow_y * flow_y);
			const size_t rgba_offset = (size_t(y) * width + x) * 4;
			rgba[rgba_offset + 0] = clamp_to_u8(128.0f + 127.0f * (flow_x / max_magnitude));
			rgba[rgba_offset + 1] = clamp_to_u8(128.0f + 127.0f * (flow_y / max_magnitude));
			rgba[rgba_offset + 2] = clamp_to_u8(255.0f * (magnitude / max_magnitude));
			rgba[rgba_offset + 3] = 255;
		}
	}

	const int result = stbi_write_png(filename, int(width), int(height), 4, rgba.data(), 0);
	assert(result != 0);
}

struct flow_stats
{
	float mean_x = 0.0f;
	float mean_y = 0.0f;
	float mean_abs_x = 0.0f;
	float mean_abs_y = 0.0f;
	float mean_magnitude = 0.0f;
	float max_magnitude = 0.0f;
	uint32_t samples = 0;
};

static flow_stats compute_flow_stats(const std::vector<uint8_t>& flow_bytes, uint32_t flow_width, uint32_t flow_height,
                                     uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, bool invert_selection)
{
	assert(flow_bytes.size() == size_t(flow_width) * size_t(flow_height) * sizeof(uint16_t) * 2);

	flow_stats stats;
	for (uint32_t y = 0; y < flow_height; ++y)
	{
		for (uint32_t x = 0; x < flow_width; ++x)
		{
			const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
			if (inside == invert_selection) continue;

			const size_t offset = (size_t(y) * flow_width + x) * sizeof(uint16_t) * 2;
			uint16_t raw_x = 0;
			uint16_t raw_y = 0;
			std::memcpy(&raw_x, flow_bytes.data() + offset, sizeof(raw_x));
			std::memcpy(&raw_y, flow_bytes.data() + offset + sizeof(raw_x), sizeof(raw_y));

			const float flow_x = half_to_float(raw_x);
			const float flow_y = half_to_float(raw_y);
			const float magnitude = std::sqrt(flow_x * flow_x + flow_y * flow_y);

			stats.mean_x += flow_x;
			stats.mean_y += flow_y;
			stats.mean_abs_x += std::fabs(flow_x);
			stats.mean_abs_y += std::fabs(flow_y);
			stats.mean_magnitude += magnitude;
			stats.max_magnitude = std::max(stats.max_magnitude, magnitude);
			stats.samples++;
		}
	}

	assert(stats.samples > 0);
	const float inv_samples = 1.0f / float(stats.samples);
	stats.mean_x *= inv_samples;
	stats.mean_y *= inv_samples;
	stats.mean_abs_x *= inv_samples;
	stats.mean_abs_y *= inv_samples;
	stats.mean_magnitude *= inv_samples;
	return stats;
}

static void validate_flow_output(const std::vector<uint8_t>& flow_bytes, uint32_t flow_width, uint32_t flow_height,
                                 uint32_t granularity, uint32_t input_square_x, uint32_t input_square_y,
                                 uint32_t square_width, uint32_t square_height, uint32_t reference_square_x,
                                 uint32_t reference_square_y, uint32_t expected_motion_x)
{
	const uint32_t input_x0 = input_square_x / granularity;
	const uint32_t input_y0 = input_square_y / granularity;
	const uint32_t input_x1 = (input_square_x + square_width + granularity - 1) / granularity;
	const uint32_t input_y1 = (input_square_y + square_height + granularity - 1) / granularity;
	const uint32_t reference_x0 = reference_square_x / granularity;
	const uint32_t reference_y0 = reference_square_y / granularity;
	const uint32_t reference_x1 = (reference_square_x + square_width + granularity - 1) / granularity;
	const uint32_t reference_y1 = (reference_square_y + square_height + granularity - 1) / granularity;

	const uint32_t motion_x0 = std::min(input_x0, reference_x0);
	const uint32_t motion_y0 = std::min(input_y0, reference_y0);
	const uint32_t motion_x1 = std::min(flow_width, std::max(input_x1, reference_x1));
	const uint32_t motion_y1 = std::min(flow_height, std::max(input_y1, reference_y1));

	const uint32_t expanded_x0 = motion_x0 > 0 ? motion_x0 - 1 : 0;
	const uint32_t expanded_y0 = motion_y0 > 0 ? motion_y0 - 1 : 0;
	const uint32_t expanded_x1 = std::min(flow_width, motion_x1 + 1);
	const uint32_t expanded_y1 = std::min(flow_height, motion_y1 + 1);

	const flow_stats motion = compute_flow_stats(flow_bytes, flow_width, flow_height,
	                                             expanded_x0, expanded_y0, expanded_x1, expanded_y1, false);
	const flow_stats background = compute_flow_stats(flow_bytes, flow_width, flow_height,
	                                                 expanded_x0, expanded_y0, expanded_x1, expanded_y1, true);

	fprintf(stderr,
	        "optical flow stats: motion mean=(%.3f, %.3f) abs=(%.3f, %.3f) mag=%.3f max=%.3f, background abs=(%.3f, %.3f) mag=%.3f\n",
	        motion.mean_x, motion.mean_y, motion.mean_abs_x, motion.mean_abs_y, motion.mean_magnitude, motion.max_magnitude,
	        background.mean_abs_x, background.mean_abs_y, background.mean_magnitude);
	fflush(stderr);

	const float min_mean_motion_x = std::max(0.5f, float(expected_motion_x) * 0.5f);
	const float min_peak_motion = std::max(0.75f, float(expected_motion_x) * 0.5f);
	assert(motion.mean_abs_x > min_mean_motion_x);
	assert(motion.max_magnitude > min_peak_motion);
	assert(motion.mean_abs_x > motion.mean_abs_y * 2.0f + 0.05f);
	assert(std::fabs(motion.mean_x) > motion.mean_abs_x * 0.75f);
}

static void assert_flow_readback_buffer(const vulkan_setup_t& vulkan, const buffer_resource& readback, const std::vector<uint8_t>& flow_bytes)
{
	if (!vulkan.vkAssertBuffer) return;

	uint32_t checksum = 0;
	const VkUpdateBufferInfoARM assert_info = {
		VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, readback.buffer, 0, VK_WHOLE_SIZE, nullptr
	};
	const VkResult result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum, "optical-flow motion vectors");
	check(result);
}

int main(int argc, char** argv)
{
	VkPhysicalDeviceDataGraphOpticalFlowFeaturesARM optical_flow_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DATA_GRAPH_OPTICAL_FLOW_FEATURES_ARM, nullptr };
	optical_flow_features.dataGraphOpticalFlow = VK_TRUE;

	VkPhysicalDeviceTensorFeaturesARM tensor_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM, &optical_flow_features };
	tensor_features.tensors = VK_TRUE;

	VkPhysicalDeviceDataGraphFeaturesARM data_graph_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DATA_GRAPH_FEATURES_ARM, using_graph_emulation_layer() ?
			reinterpret_cast<void*>(&tensor_features) : reinterpret_cast<void*>(&optical_flow_features) };
	data_graph_features.dataGraph = VK_TRUE;

	vulkan_req_t reqs;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&data_graph_features);
	reqs.device_extensions.push_back(VK_ARM_DATA_GRAPH_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_ARM_DATA_GRAPH_OPTICAL_FLOW_EXTENSION_NAME);
	if (using_graph_emulation_layer()) reqs.device_extensions.push_back(VK_ARM_TENSORS_EXTENSION_NAME);
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.required_queue_flags = VK_QUEUE_DATA_GRAPH_BIT_ARM;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_data_graph_optical_flow", reqs);

	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceQueueFamilyDataGraphEngineOperationPropertiesARM);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceQueueFamilyDataGraphOpticalFlowImageFormatsARM);
	MAKEDEVICEPROCADDR(vulkan, vkCreateDataGraphPipelinesARM);
	MAKEDEVICEPROCADDR(vulkan, vkCreateDataGraphPipelineSessionARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetDataGraphPipelineSessionBindPointRequirementsARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetDataGraphPipelineSessionMemoryRequirementsARM);
	MAKEDEVICEPROCADDR(vulkan, vkBindDataGraphPipelineSessionMemoryARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyDataGraphPipelineSessionARM);
	MAKEDEVICEPROCADDR(vulkan, vkCmdDispatchDataGraphARM);

	optical_flow_instance_functions instance_functions = {
		pf_vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM,
		pf_vkGetPhysicalDeviceQueueFamilyDataGraphEngineOperationPropertiesARM,
		pf_vkGetPhysicalDeviceQueueFamilyDataGraphOpticalFlowImageFormatsARM,
	};
	optical_flow_device_functions device_functions = {
		pf_vkCreateDataGraphPipelinesARM,
		pf_vkCreateDataGraphPipelineSessionARM,
		pf_vkGetDataGraphPipelineSessionBindPointRequirementsARM,
		pf_vkGetDataGraphPipelineSessionMemoryRequirementsARM,
		pf_vkBindDataGraphPipelineSessionMemoryARM,
		pf_vkDestroyDataGraphPipelineSessionARM,
		pf_vkCmdDispatchDataGraphARM,
	};

	uint32_t operation_count = 0;
	VkResult result = instance_functions.get_queue_family_data_graph_properties(
		vulkan.physical, vulkan.queue_family_index, &operation_count, nullptr);
	check(result);
	if (operation_count == 0) return skip_not_supported(vulkan, "No data-graph operations reported for selected queue family");

	std::vector<VkQueueFamilyDataGraphPropertiesARM> operations(operation_count);
	for (auto& operation : operations)
	{
		operation = { VK_STRUCTURE_TYPE_QUEUE_FAMILY_DATA_GRAPH_PROPERTIES_ARM, nullptr };
	}
	result = instance_functions.get_queue_family_data_graph_properties(
		vulkan.physical, vulkan.queue_family_index, &operation_count, operations.data());
	check(result);

	const VkQueueFamilyDataGraphPropertiesARM* optical_flow_operation = nullptr;
	for (const auto& operation : operations)
	{
		if (operation.operation.operationType == VK_PHYSICAL_DEVICE_DATA_GRAPH_OPERATION_TYPE_OPTICAL_FLOW_ARM)
		{
			optical_flow_operation = &operation;
			break;
		}
	}
	if (!optical_flow_operation) return skip_not_supported(vulkan, "Selected queue family does not expose optical-flow operation support");

	VkQueueFamilyDataGraphOpticalFlowPropertiesARM optical_flow_properties = {
		VK_STRUCTURE_TYPE_QUEUE_FAMILY_DATA_GRAPH_OPTICAL_FLOW_PROPERTIES_ARM, nullptr };
	result = instance_functions.get_engine_operation_properties(
		vulkan.physical, vulkan.queue_family_index, optical_flow_operation, reinterpret_cast<VkBaseOutStructure*>(&optical_flow_properties));
	check(result);

	const uint32_t granularity = choose_grid_granularity(optical_flow_properties.supportedOutputGridSizes);
	if (granularity == 0) return skip_not_supported(vulkan, "Optical-flow operation reported no supported output grid sizes");

	const VkDataGraphOpticalFlowGridSizeFlagsARM output_grid_size = grid_flag_from_granularity(granularity);
	const bool enable_hint = optical_flow_properties.hintSupported == VK_TRUE &&
	                         (optical_flow_properties.supportedHintGridSizes & output_grid_size) != 0;
	bool enable_cost = optical_flow_properties.costSupported == VK_TRUE;

	const std::vector<VkFormat> input_formats = query_optical_flow_formats(
		vulkan, instance_functions, *optical_flow_operation, VK_DATA_GRAPH_OPTICAL_FLOW_IMAGE_USAGE_INPUT_BIT_ARM);
	const std::vector<VkFormat> flow_formats = query_optical_flow_formats(
		vulkan, instance_functions, *optical_flow_operation, VK_DATA_GRAPH_OPTICAL_FLOW_IMAGE_USAGE_OUTPUT_BIT_ARM);
	if (enable_hint)
	{
		const std::vector<VkFormat> hint_formats = query_optical_flow_formats(
			vulkan, instance_functions, *optical_flow_operation, VK_DATA_GRAPH_OPTICAL_FLOW_IMAGE_USAGE_HINT_BIT_ARM);
		if (!contains_format(hint_formats, VK_FORMAT_R16G16_SFLOAT))
		{
			return skip_not_supported(vulkan, "Optical-flow hint path is supported, but R16G16_SFLOAT hint images are not");
		}
	}

	VkFormat cost_format = VK_FORMAT_UNDEFINED;
	if (enable_cost)
	{
		const std::vector<VkFormat> cost_formats = query_optical_flow_formats(
			vulkan, instance_functions, *optical_flow_operation, VK_DATA_GRAPH_OPTICAL_FLOW_IMAGE_USAGE_COST_BIT_ARM);
		cost_format = choose_required_format(cost_formats, VK_FORMAT_R16_UINT);
		if (cost_format == VK_FORMAT_UNDEFINED)
		{
			enable_cost = false;
		}
	}

	const VkFormat input_format = choose_required_format(input_formats, VK_FORMAT_R8_UNORM);
	const VkFormat flow_format = choose_required_format(flow_formats, VK_FORMAT_R16G16_SFLOAT);
	if (input_format == VK_FORMAT_UNDEFINED) return skip_not_supported(vulkan, "Test requires VK_FORMAT_R8_UNORM as an optical-flow input format");
	if (flow_format == VK_FORMAT_UNDEFINED) return skip_not_supported(vulkan, "Test requires VK_FORMAT_R16G16_SFLOAT as an optical-flow output format");

	uint32_t width = std::max(64u, optical_flow_properties.minWidth);
	uint32_t height = std::max(64u, optical_flow_properties.minHeight);
	width = round_up_to_multiple(width, granularity);
	height = round_up_to_multiple(height, granularity);
	if (width > optical_flow_properties.maxWidth || height > optical_flow_properties.maxHeight)
	{
		return skip_not_supported(vulkan, "Optical-flow size limits do not allow the deterministic test dimensions");
	}

	const uint32_t flow_width = width / granularity;
	const uint32_t flow_height = height / granularity;
	assert(flow_width > 0 && flow_height > 0);

	bench_start_iteration(vulkan.bench);

	VkDescriptorSetLayoutBinding bindings[5] = {};
	const uint32_t descriptor_count = 3u + (enable_hint ? 1u : 0u) + (enable_cost ? 1u : 0u);
	for (uint32_t i = 0; i < descriptor_count; ++i)
	{
		bindings[i].binding = i;
		bindings[i].descriptorCount = 1;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
	}

	VkDescriptorSetLayoutCreateInfo set_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	set_layout_info.bindingCount = descriptor_count;
	set_layout_info.pBindings = bindings;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	result = vkCreateDescriptorSetLayout(vulkan.device, &set_layout_info, nullptr, &descriptor_set_layout);
	check(result);

	VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &pipeline_layout);
	check(result);

	VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptor_count };
	VkDescriptorPoolCreateInfo descriptor_pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	descriptor_pool_info.maxSets = 1;
	descriptor_pool_info.poolSizeCount = 1;
	descriptor_pool_info.pPoolSizes = &pool_size;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	result = vkCreateDescriptorPool(vulkan.device, &descriptor_pool_info, nullptr, &descriptor_pool);
	check(result);

	VkDescriptorSetAllocateInfo descriptor_set_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	descriptor_set_info.descriptorPool = descriptor_pool;
	descriptor_set_info.descriptorSetCount = 1;
	descriptor_set_info.pSetLayouts = &descriptor_set_layout;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	result = vkAllocateDescriptorSets(vulkan.device, &descriptor_set_info, &descriptor_set);
	check(result);

	std::vector<VkDataGraphPipelineResourceInfoImageLayoutARM> resource_layouts(descriptor_count);
	std::vector<VkDataGraphPipelineResourceInfoARM> resource_infos(descriptor_count);
	for (uint32_t i = 0; i < descriptor_count; ++i)
	{
		resource_layouts[i] = { VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_RESOURCE_INFO_IMAGE_LAYOUT_ARM, nullptr };
		resource_layouts[i].layout = VK_IMAGE_LAYOUT_GENERAL;
		resource_infos[i] = { VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_RESOURCE_INFO_ARM, &resource_layouts[i] };
		resource_infos[i].descriptorSet = 0;
		resource_infos[i].binding = i;
		resource_infos[i].arrayElement = 0;
	}

	std::vector<VkDataGraphPipelineSingleNodeConnectionARM> connections;
	connections.push_back({ VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SINGLE_NODE_CONNECTION_ARM, nullptr, 0, 0,
		VK_DATA_GRAPH_PIPELINE_NODE_CONNECTION_TYPE_OPTICAL_FLOW_INPUT_ARM });
	connections.push_back({ VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SINGLE_NODE_CONNECTION_ARM, nullptr, 0, 1,
		VK_DATA_GRAPH_PIPELINE_NODE_CONNECTION_TYPE_OPTICAL_FLOW_REFERENCE_ARM });
	connections.push_back({ VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SINGLE_NODE_CONNECTION_ARM, nullptr, 0, 2,
		VK_DATA_GRAPH_PIPELINE_NODE_CONNECTION_TYPE_OPTICAL_FLOW_FLOW_VECTOR_ARM });
	if (enable_hint)
	{
		connections.push_back({ VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SINGLE_NODE_CONNECTION_ARM, nullptr, 0, 3,
			VK_DATA_GRAPH_PIPELINE_NODE_CONNECTION_TYPE_OPTICAL_FLOW_HINT_ARM });
	}
	if (enable_cost)
	{
		const uint32_t cost_binding = enable_hint ? 4u : 3u;
		connections.push_back({ VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SINGLE_NODE_CONNECTION_ARM, nullptr, 0, cost_binding,
			VK_DATA_GRAPH_PIPELINE_NODE_CONNECTION_TYPE_OPTICAL_FLOW_COST_ARM });
	}

	VkDataGraphPipelineOpticalFlowCreateInfoARM optical_flow_create_info = {
		VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_OPTICAL_FLOW_CREATE_INFO_ARM, nullptr };
	optical_flow_create_info.width = width;
	optical_flow_create_info.height = height;
	optical_flow_create_info.imageFormat = input_format;
	optical_flow_create_info.flowVectorFormat = flow_format;
	optical_flow_create_info.costFormat = cost_format;
	optical_flow_create_info.outputGridSize = output_grid_size;
	optical_flow_create_info.hintGridSize = enable_hint ? output_grid_size : 0;
	optical_flow_create_info.performanceLevel = VK_DATA_GRAPH_OPTICAL_FLOW_PERFORMANCE_LEVEL_MEDIUM_ARM;
	if (enable_hint) optical_flow_create_info.flags |= VK_DATA_GRAPH_OPTICAL_FLOW_CREATE_ENABLE_HINT_BIT_ARM;
	if (enable_cost) optical_flow_create_info.flags |= VK_DATA_GRAPH_OPTICAL_FLOW_CREATE_ENABLE_COST_BIT_ARM;

	VkDataGraphPipelineSingleNodeCreateInfoARM single_node_create_info = {
		VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_SINGLE_NODE_CREATE_INFO_ARM, &optical_flow_create_info };
	single_node_create_info.nodeType = VK_DATA_GRAPH_PIPELINE_NODE_TYPE_OPTICAL_FLOW_ARM;
	single_node_create_info.connectionCount = static_cast<uint32_t>(connections.size());
	single_node_create_info.pConnections = connections.data();

	VkDataGraphPipelineCreateInfoARM pipeline_create_info = {
		VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_CREATE_INFO_ARM, &single_node_create_info };
	pipeline_create_info.layout = pipeline_layout;
	pipeline_create_info.resourceInfoCount = static_cast<uint32_t>(resource_infos.size());
	pipeline_create_info.pResourceInfos = resource_infos.data();

	VkPipeline pipeline = VK_NULL_HANDLE;
	result = device_functions.create_pipelines(vulkan.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, "data_graph_optical_flow_pipeline");

	data_graph_session session = create_data_graph_session(vulkan, device_functions, pipeline);

	image_resource input_image = create_image_resource(
		vulkan, input_format, width, height, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		"data_graph_optical_flow_input");
	image_resource reference_image = create_image_resource(
		vulkan, input_format, width, height, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		"data_graph_optical_flow_reference");
	image_resource flow_image = create_image_resource(
		vulkan, flow_format, flow_width, flow_height,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		"data_graph_optical_flow_flow");
	image_resource hint_image = {};
	image_resource cost_image = {};
	if (enable_hint)
	{
		hint_image = create_image_resource(
			vulkan, flow_format, flow_width, flow_height, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			"data_graph_optical_flow_hint");
	}
	if (enable_cost)
	{
		cost_image = create_image_resource(
			vulkan, cost_format, flow_width, flow_height, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			"data_graph_optical_flow_cost");
	}

	std::vector<VkDescriptorImageInfo> image_infos;
	image_infos.reserve(descriptor_count);
	image_infos.push_back({ VK_NULL_HANDLE, input_image.view, VK_IMAGE_LAYOUT_GENERAL });
	image_infos.push_back({ VK_NULL_HANDLE, reference_image.view, VK_IMAGE_LAYOUT_GENERAL });
	image_infos.push_back({ VK_NULL_HANDLE, flow_image.view, VK_IMAGE_LAYOUT_GENERAL });
	if (enable_hint) image_infos.push_back({ VK_NULL_HANDLE, hint_image.view, VK_IMAGE_LAYOUT_GENERAL });
	if (enable_cost) image_infos.push_back({ VK_NULL_HANDLE, cost_image.view, VK_IMAGE_LAYOUT_GENERAL });

	std::vector<VkWriteDescriptorSet> writes(descriptor_count);
	for (uint32_t i = 0; i < descriptor_count; ++i)
	{
		writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
		writes[i].dstSet = descriptor_set;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[i].pImageInfo = &image_infos[i];
	}
	vkUpdateDescriptorSets(vulkan.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

	const uint32_t motion_x = granularity;
	const uint32_t square_width = round_up_to_multiple(std::max(16u, width / 2u), granularity);
	const uint32_t square_height = round_up_to_multiple(std::max(16u, height / 2u), granularity);
	assert(square_width + motion_x <= width);
	assert(square_height <= height);
	const uint32_t input_square_x = (width - square_width - motion_x) / 2u;
	const uint32_t input_square_y = (height - square_height) / 2u;
	const uint32_t reference_square_x = input_square_x + motion_x;
	const uint32_t reference_square_y = input_square_y;
	const std::vector<uint8_t> input_frame = make_square_frame(
		width, height, input_square_x, input_square_y, square_width, square_height);
	const std::vector<uint8_t> reference_frame = make_square_frame(
		width, height, reference_square_x, reference_square_y, square_width, square_height);
	buffer_resource input_upload = create_buffer_resource(
		vulkan, input_frame.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "data_graph_optical_flow_input_upload");
	buffer_resource reference_upload = create_buffer_resource(
		vulkan, reference_frame.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "data_graph_optical_flow_reference_upload");
	write_buffer_resource(vulkan, input_upload, input_frame);
	write_buffer_resource(vulkan, reference_upload, reference_frame);

	const VkDeviceSize readback_size = VkDeviceSize(flow_width) * VkDeviceSize(flow_height) * sizeof(uint16_t) * 2;
	buffer_resource readback = create_buffer_resource(
		vulkan, readback_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "data_graph_optical_flow_readback");

	VkCommandPoolCreateInfo command_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &command_pool_info, nullptr, &command_pool);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)command_pool, "data_graph_optical_flow_command_pool");

	VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_info.commandPool = command_pool;
	command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_info, &command_buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer, "data_graph_optical_flow_command_buffer");

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	std::vector<VkImageMemoryBarrier2> to_upload_and_general;
	add_general_image_barrier(to_upload_and_general, input_image.image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	add_general_image_barrier(to_upload_and_general, reference_image.image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	add_general_image_barrier(to_upload_and_general, flow_image.image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	if (enable_hint)
	{
		add_general_image_barrier(to_upload_and_general, hint_image.image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	}
	if (enable_cost)
	{
		add_general_image_barrier(to_upload_and_general, cost_image.image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	}
	cmd_pipeline_barrier2(command_buffer, to_upload_and_general);

	VkBufferImageCopy upload_region = {};
	upload_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	upload_region.imageSubresource.layerCount = 1;
	upload_region.imageExtent = { width, height, 1 };
	vkCmdCopyBufferToImage(command_buffer, input_upload.buffer, input_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload_region);
	vkCmdCopyBufferToImage(command_buffer, reference_upload.buffer, reference_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload_region);

	const VkImageSubresourceRange subresource_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	const VkClearColorValue flow_seed_color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
	const VkClearColorValue hint_color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
	const VkClearColorValue cost_seed_color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

	vkCmdClearColorImage(command_buffer, flow_image.image, VK_IMAGE_LAYOUT_GENERAL, &flow_seed_color, 1, &subresource_range);
	if (enable_hint) vkCmdClearColorImage(command_buffer, hint_image.image, VK_IMAGE_LAYOUT_GENERAL, &hint_color, 1, &subresource_range);
	if (enable_cost) vkCmdClearColorImage(command_buffer, cost_image.image, VK_IMAGE_LAYOUT_GENERAL, &cost_seed_color, 1, &subresource_range);

	std::vector<VkImageMemoryBarrier2> to_dispatch;
	add_general_image_barrier(to_dispatch, input_image.image, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	add_general_image_barrier(to_dispatch, reference_image.image, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	add_general_image_barrier(to_dispatch, flow_image.image, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	if (enable_hint)
	{
		add_general_image_barrier(to_dispatch, hint_image.image, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}
	if (enable_cost)
	{
		add_general_image_barrier(to_dispatch, cost_image.image, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}
	cmd_pipeline_barrier2(command_buffer, to_dispatch);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_DATA_GRAPH_ARM, pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_DATA_GRAPH_ARM, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
	device_functions.cmd_dispatch(command_buffer, session.session, nullptr);

	std::vector<VkImageMemoryBarrier2> between_dispatches;
	add_general_image_barrier(between_dispatches, input_image.image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	add_general_image_barrier(between_dispatches, reference_image.image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	add_general_image_barrier(between_dispatches, flow_image.image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	if (enable_hint)
	{
		add_general_image_barrier(between_dispatches, hint_image.image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}
	if (enable_cost)
	{
		add_general_image_barrier(between_dispatches, cost_image.image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}
	cmd_pipeline_barrier2(command_buffer, between_dispatches);

	VkDataGraphPipelineOpticalFlowDispatchInfoARM optical_flow_dispatch_info = {
		VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_OPTICAL_FLOW_DISPATCH_INFO_ARM, nullptr };
	optical_flow_dispatch_info.flags = VK_DATA_GRAPH_OPTICAL_FLOW_EXECUTE_DISABLE_TEMPORAL_HINTS_BIT_ARM;
	optical_flow_dispatch_info.meanFlowL1NormHint = 1;

	VkDataGraphPipelineDispatchInfoARM dispatch_info = { VK_STRUCTURE_TYPE_DATA_GRAPH_PIPELINE_DISPATCH_INFO_ARM, &optical_flow_dispatch_info };
	device_functions.cmd_dispatch(command_buffer, session.session, &dispatch_info);

	std::vector<VkImageMemoryBarrier2> to_readback;
	add_general_image_barrier(to_readback, flow_image.image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	cmd_pipeline_barrier2(command_buffer, to_readback);

	VkBufferImageCopy copy_region = {};
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent = { flow_width, flow_height, 1 };
	vkCmdCopyImageToBuffer(command_buffer, flow_image.image, VK_IMAGE_LAYOUT_GENERAL, readback.buffer, 1, &copy_region);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fence = VK_NULL_HANDLE;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	void* mapped = nullptr;
	result = vkMapMemory(vulkan.device, readback.memory, 0, readback_size, 0, &mapped);
	check(result);
	std::vector<uint8_t> flow_bytes(size_t(readback_size), uint8_t{ 0 });
	std::memcpy(flow_bytes.data(), mapped, size_t(readback_size));
	vkUnmapMemory(vulkan.device, readback.memory);
	if (reqs.options.count("image_output"))
	{
		save_r8_image_png("data_graph_optical_flow_input.png", input_frame, width, height);
		save_r8_image_png("data_graph_optical_flow_reference.png", reference_frame, width, height);
		save_flow_visualization_png("data_graph_optical_flow_flow.png", flow_bytes, flow_width, flow_height);
	}
	validate_flow_output(flow_bytes, flow_width, flow_height, granularity,
	                     input_square_x, input_square_y, square_width, square_height,
	                     reference_square_x, reference_square_y, motion_x);
	assert_flow_readback_buffer(vulkan, readback, flow_bytes);

	bench_stop_iteration(vulkan.bench);

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	destroy_buffer_resource(vulkan, readback);
	destroy_buffer_resource(vulkan, reference_upload);
	destroy_buffer_resource(vulkan, input_upload);
	destroy_image_resource(vulkan, cost_image);
	destroy_image_resource(vulkan, hint_image);
	destroy_image_resource(vulkan, flow_image);
	destroy_image_resource(vulkan, reference_image);
	destroy_image_resource(vulkan, input_image);
	destroy_data_graph_session(vulkan, device_functions, session);
	vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyDescriptorPool(vulkan.device, descriptor_pool, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, descriptor_set_layout, nullptr);

	test_done(vulkan);
	return 0;
}
