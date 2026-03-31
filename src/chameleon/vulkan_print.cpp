#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <algorithm>
#include <bitset>
#include <string>
#include <vector>
#include <list>
#include <sstream>
#include <unordered_map>

#include "util.h"
#include "vulkan_print.h"
#include "vulkan_auto.h"
#include "vkjson.h"
#include "tostring.h"
#include <sys/stat.h>

static std::unordered_map<long, long> threads;
static std::unordered_set<int> frames_of_interest;
static int verbose = 0;

static const bool deterministic = get_env_int("CHAMELEON_DETERMINISTIC", 0);

std::unordered_set<int> get_env_ints(const char* name)
{
	std::unordered_set<int> retval;
	const char* tmpstr = getenv(name);
	if (tmpstr)
	{
		std::stringstream ss(tmpstr);
		int i;
		while (ss >> i)
		{
			retval.insert(i);
			if (ss.peek() == ',')
			{
				ss.ignore();
			}
		}
	}
	return retval;
}

static Json::Value json_base(const cVkBase& base)
{
	Json::Value json;
	json["frame_created"] = base.created_frame;
	if (!deterministic) json["accessed_by_threads"] = Json::arrayValue;
	json["uid"] = base.uid;
	if (frames_of_interest.size() > 0)
	{
		json["frames_of_interest_usage"] = naive_intersect(base.used_in_frame, frames_of_interest);
		json["frames_of_interest_involved"] = naive_intersect(base.used_in_frame_transitive, frames_of_interest);
	}
	int hits = 0;
	for (const auto& t : base.accessed_by_thread)
	{
		if (!deterministic) json["accessed_by_threads"].append((Json::Value::UInt64)t);
		threads[t]++;
		hits++;
	}
	if (deterministic) json["accessed_by_threads"] = hits; // just the number of threads accessing, not their non-deterministic IDs
	if (base.destroyed_frame != -1)
	{
		json["frame_destroyed"] = base.destroyed_frame;
	}
	if (!base.marker_name.empty())
	{
		json["name"] = base.marker_name;
	}
	if (base.tagName != 0)
	{
		json["tag_name"] = (Json::Value::UInt64)base.tagName;
	}
	return json;
}

static void append_if_relevant(Json::Value& dst, const Json::Value& src, const cVkBase& base, int verbosity_level = 0)
{
	bool used = naive_intersect(base.used_in_frame_transitive, frames_of_interest);
	if (frames_of_interest.size() > 0) // if limiting by frame, bump verbosity requirement
	{
		verbosity_level += 1;
	}
	if (verbose > verbosity_level || used)
	{
		dst.append(src);
	}
}

void write_file(const std::string& filename, const void *buffer, size_t size)
{
	FILE* fp = fopen(filename.c_str(), "w");
	if (!fp)
	{
		fprintf(stderr, "Failed to open %s: %s", filename.c_str(), strerror(errno));
		return;
	}
	size_t written = 0;
	do
	{
		written = fwrite(buffer, size, 1, fp);
	} while (written != size && ferror(fp) == EAGAIN);
	fclose(fp);
}

template<typename T>
static std::string set_to_comma_string(const std::unordered_set<T>& a)
{
	std::vector<T> elems(a.begin(), a.end());
	std::sort(elems.begin(), elems.end()); // make deterministic
	const int size = elems.size();
	std::string retval;
	int count = 0;
	for (const auto i : elems)
	{
		retval += _to_string(i);
		count++;
		if (count < size)
		{
			retval += ',';
		}
	}
	return retval;
}

static Json::Value histogram_to_json(const std::map<std::string, uint64_t>& histogram)
{
	Json::Value histogramv;
	for (const auto& pair : histogram)
	{
		histogramv[pair.first] = (Json::Value::UInt64)pair.second;
	}
	return histogramv;
}

static Json::Value histogram_to_json(const std::map<uint64_t, uint64_t>& histogram)
{
	Json::Value histogramv;
	for (const auto& pair : histogram)
	{
		histogramv[_to_string(pair.first)] = (Json::Value::UInt64)pair.second;
	}
	return histogramv;
}

void json_overview(const std::string& report_name, cVkInstance* instance, bool hw_info)
{
	instance->update(); // update transitive information
	int dump_shaders = get_env_int("CHAMELEON_SHADERDUMP", 0);
	verbose = get_env_int("CHAMELEON_VERBOSITY", 0); // verbosity level
	frames_of_interest = get_env_ints("CHAMELEON_FRAMES");
	Json::Value result = json_base(*instance);
	Json::Value chameleon_settings;
	chameleon_settings["verbosity"] = verbose;
	chameleon_settings["dump_shaders"] = dump_shaders;
	if (frames_of_interest.size() > 0)
	{
		chameleon_settings["frames_of_interest"] = set_to_comma_string(frames_of_interest);
	}
	const char* source = getenv("CHAMELEON_SOURCE");
	if (source) result["source"] = source;
	int priority = get_env_int("CHAMELEON_PRIORITY", -1);
	if (priority != -1) result["priority"] = priority;
	result["chameleon_settings"] = chameleon_settings;
	result["instances"] = (unsigned)instance->instance_counter;
	result["instance"] = instance->instance_id;
	result["physical_devices"] = Json::arrayValue;
	result["surfaces"] = Json::arrayValue;
	result["displays"] = Json::arrayValue;
	result["enabled_instance_extensions"] = Json::arrayValue;
	for (const std::string& extension : instance->enabledExtensions)
	{
		result["enabled_instance_extensions"].append(extension);
	}
	for (cVkPhysicalDevice& pdev : instance->GPUs)
	{
		Json::Value pdv = json_base(pdev);
		pdv["device_name"] = pdev.properties.deviceName;
		pdv["android_hw_level"] = android_hw_level(*reinterpret_cast<VkPhysicalDeviceFeatures*>(pdev.features[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2].first));
		pdv["gpu_path"] = getenv("CHAMELEON_GPU");
		pdv["devices"] = Json::arrayValue;
		for (cVkDevice& dev : pdev.devices)
		{
			Json::Value dv = json_base(dev);
			dv["enabled_device_extensions"] = Json::arrayValue;
			for (const std::string& extension : dev.enabledExtensions)
			{
				dv["enabled_device_extensions"].append(extension);
			}
			dv["device_memory_allocations"] = (Json::Value::UInt64)dev.deviceMemory.size();
			dv["device_memory_max_allocated"] = Json::arrayValue;
			for (const auto& pair : dev.memory_highest)
			{
				Json::Value v;
				v["memory_type_index"] = pair.first; // TBD, add what type of memory this is
				v["max_memory_allocated"] = (Json::Value::UInt64)pair.second;
				dv["device_memory_max_allocated"].append(v);
			}
			dv["fences_count"] = (Json::Value::UInt64)dev.fences.size();
			dv["swapchains"] = Json::arrayValue;
			for (const cVkSwapchainKHR& q : dev.swapchains)
			{
				Json::Value chain = json_base(q);
				chain["images"] = (Json::Value::UInt64)q.images.size();
				chain["clipped"] = q.clipped;
				chain["present_mode"] = VkPresentModeKHR_to_string(q.presentMode);
				chain["image_format"] = VkFormat_to_string(q.imageFormat);
				append_if_relevant(dv["swapchains"], chain, q);
			}
			dv["semaphores_count"] = (Json::Value::UInt64)dev.semaphores.size();
			dv["semaphores"] = Json::arrayValue;
			for (const cVkSemaphore& s : dev.semaphores)
			{
				Json::Value p = json_base(s);
				p["flags"] = VkSemaphoreCreateFlags_to_string(s.flags);
				dv["semaphores"].append(p);
			}
			dv["events_count"] = (Json::Value::UInt64)dev.events.size();

			// Images
			dv["images_count"] = (Json::Value::UInt64)dev.images.size();
			int count_tx_src_bit = 0;
			int count_tx_dst_bit = 0;
			int count_sampled_bit = 0;
			int count_transient_bit = 0;
			int count_input_bit = 0;
			int count_usage_bit = 0;
			int count_color_bit = 0;
			int count_depth_stencil_bit = 0;
			int count_usage_but_not_color_bit = 0;
			int count_usage_but_not_depth_stencil_bit = 0;
			std::map<std::string, uint64_t> image_flag_histogram;
			std::map<std::string, uint64_t> image_type_histogram;
			std::map<std::string, uint64_t> image_format_histogram;
			std::map<std::string, uint64_t> image_size_histogram;
			std::map<std::string, uint64_t> image_samples_histogram;
			std::map<std::string, uint64_t> image_tiling_histogram;
			std::map<std::string, uint64_t> image_usage_histogram;
			std::map<std::string, uint64_t> image_sharing_histogram;
			std::map<uint64_t, uint64_t> image_miplevels_histogram;
			std::map<uint64_t, uint64_t> image_arraylayers_histogram;
			for (const cVkImage& cv : dev.images)
			{
				if (cv.imageType == VK_IMAGE_TYPE_MAX_ENUM) continue; // swapchain

				if (cv.flags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) count_tx_src_bit++;
				if (cv.flags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) count_tx_dst_bit++;
				if (cv.flags & VK_IMAGE_USAGE_SAMPLED_BIT) count_sampled_bit++;
				if (cv.flags & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) count_transient_bit++;
				if (cv.flags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) count_input_bit++;
				if (cv.flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) count_color_bit++;
				if (cv.flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) count_depth_stencil_bit++;
				if (cv.flags & VK_IMAGE_USAGE_STORAGE_BIT) count_usage_bit++;
				if ((cv.flags & VK_IMAGE_USAGE_STORAGE_BIT) && !(cv.flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) count_usage_but_not_color_bit++;
				if ((cv.flags & VK_IMAGE_USAGE_STORAGE_BIT) && !(cv.flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) count_usage_but_not_depth_stencil_bit++;

				image_flag_histogram[VkImageCreateFlags_to_string(cv.flags)]++;
				image_type_histogram[VkImageType_to_string(cv.imageType)]++;
				image_format_histogram[VkFormat_to_string(cv.format)]++;
				std::string dim = std::to_string(cv.extent.width) + "x" + std::to_string(cv.extent.height) + "x" + std::to_string(cv.extent.depth);
				image_size_histogram[dim]++;
				image_samples_histogram[VkSampleCountFlags_to_string(cv.samples)]++;
				image_tiling_histogram[VkImageTiling_to_string(cv.tiling)]++;
				image_usage_histogram[VkImageUsageFlags_to_string(cv.usage)]++;
				image_sharing_histogram[VkSharingMode_to_string(cv.sharingMode)]++;
				image_miplevels_histogram[cv.mipLevels]++;
				image_arraylayers_histogram[cv.arrayLayers]++;
			}
			dv["image_flag_histogram"] = histogram_to_json(image_flag_histogram);
			dv["image_type_histogram"] = histogram_to_json(image_type_histogram);
			dv["image_format_histogram"] = histogram_to_json(image_format_histogram);
			dv["image_size_histogram"] = histogram_to_json(image_size_histogram);
			dv["image_samples_histogram"] = histogram_to_json(image_samples_histogram);
			dv["image_tiling_histogram"] = histogram_to_json(image_tiling_histogram);
			dv["image_usage_histogram"] = histogram_to_json(image_usage_histogram);
			dv["image_sharing_histogram"] = histogram_to_json(image_sharing_histogram);
			dv["image_miplevels_histogram"] = histogram_to_json(image_miplevels_histogram);
			dv["image_arraylayers_histogram"] = histogram_to_json(image_arraylayers_histogram);
			dv["images_with_sampled_bit"] = count_sampled_bit;
			dv["images_with_tx_src_bit"] = count_tx_src_bit;
			dv["images_with_tx_dst_bit"] = count_tx_dst_bit;
			dv["images_with_depth_stencil_bit"] = count_depth_stencil_bit;
			dv["images_with_transient_bit"] = count_transient_bit;
			dv["images_with_input_bit"] = count_input_bit;
			dv["images_with_usage_bit"] = count_usage_bit;
			dv["images_with_usage_but_not_color_bit"] = count_usage_but_not_color_bit;
			dv["images_with_usage_but_not_depth_stencil_bit"] = count_usage_but_not_depth_stencil_bit;

			// Image views
			dv["image_views_count"] = (Json::Value::UInt64)dev.imageViews.size();
			std::map<std::string, uint64_t> imageview_flag_histogram;
			std::map<std::string, uint64_t> imageview_type_histogram;
			std::map<std::string, uint64_t> imageview_format_histogram;
			for (const cVkImageView& cv : dev.imageViews)
			{
				imageview_flag_histogram[VkImageViewCreateFlags_to_string(cv.flags)]++;
				imageview_type_histogram[VkImageViewType_to_string(cv.viewType)]++;
				imageview_format_histogram[VkFormat_to_string(cv.format)]++;
			}
			dv["image_view_flag_histogram"] = histogram_to_json(imageview_flag_histogram);
			dv["image_view_type_histogram"] = histogram_to_json(imageview_type_histogram);
			dv["image_view_format_histogram"] = histogram_to_json(imageview_format_histogram);

			// Buffers
			dv["buffers_count"] = (Json::Value::UInt64)dev.buffers.size();
			std::map<std::string, uint64_t> buffer_flag_histogram;
			std::map<std::string, uint64_t> buffer_usage_histogram;
			std::map<std::string, uint64_t> buffer_sharing_histogram;
			for (const cVkBuffer& buf : dev.buffers)
			{
				buffer_flag_histogram[VkBufferCreateFlags_to_string(buf.flags)]++;
				buffer_usage_histogram[VkBufferUsageFlags_to_string(buf.usage)]++;
				buffer_sharing_histogram[VkSharingMode_to_string(buf.sharingMode)]++;
			}
			dv["buffer_flag_histogram"] = histogram_to_json(buffer_flag_histogram);
			dv["buffer_usage_histogram"] = histogram_to_json(buffer_usage_histogram);
			dv["buffer_sharing_histogram"] = histogram_to_json(buffer_sharing_histogram);

			dv["buffer_views_count"] = (Json::Value::UInt64)dev.bufferViews.size();
			dv["query_pools_count"] = (Json::Value::UInt64)dev.queryPools.size();

			// Pipeline caches
			dv["pipeline_caches_count"] = (Json::Value::UInt64)dev.pipelineCaches.size();
			dv["pipeline_caches"] = Json::arrayValue;
			std::map<uint64_t, uint64_t> pipelinecache_pipelines_histogram;
			for (const cVkPipelineCache& cache : dev.pipelineCaches)
			{
				pipelinecache_pipelines_histogram[cache.pipelines.size()]++;
			}
			dv["pipelinecache_pipeline_count_histogram"] = histogram_to_json(pipelinecache_pipelines_histogram);

			// Pipeline layouts
			dv["pipeline_layouts_count"] = (Json::Value::UInt64)dev.pipelineLayouts.size();
			dv["pipeline_layouts"] = Json::arrayValue;
			std::map<uint64_t, uint64_t> pipelinelayout_sets_histogram;
			std::map<uint64_t, uint64_t> pipelinelayout_pushconstant_ranges_histogram;
			std::map<std::string, uint64_t> pipelinelayout_flags_histogram;
			for (const cVkPipelineLayout& layout : dev.pipelineLayouts)
			{
				Json::Value layoutv = json_base(layout);
				pipelinelayout_flags_histogram[VkPipelineLayoutCreateFlags_to_string(layout.flags)]++;
				pipelinelayout_sets_histogram[layout.setLayouts.size()]++;
				pipelinelayout_pushconstant_ranges_histogram[layout.pushConstantRanges.size()]++;
			}
			dv["pipelinelayout_sets_histogram"] = histogram_to_json(pipelinelayout_sets_histogram);
			dv["pipelinelayout_pushconstant_ranges_histogram"] = histogram_to_json(pipelinelayout_pushconstant_ranges_histogram);
			dv["pipelinelayout_flags_histogram"] = histogram_to_json(pipelinelayout_flags_histogram);

			// Samplers
			dv["samplers_count"] = (Json::Value::UInt64)dev.samplers.size();
			dv["samplers"] = Json::arrayValue;
			std::map<std::string, uint64_t> sampler_address_mode_histogram;
			std::map<std::string, uint64_t> sampler_flags_histogram;
			std::map<std::string, uint64_t> sampler_mag_filter_histogram;
			std::map<std::string, uint64_t> sampler_min_filter_histogram;
			std::map<std::string, uint64_t> sampler_mipmap_histogram;
			std::map<std::string, uint64_t> sampler_compareop_histogram;
			std::map<std::string, uint64_t> sampler_compareenable_histogram;
			std::map<std::string, uint64_t> sampler_bordercolor_histogram;
			std::map<std::string, uint64_t> sampler_anisotropy_histogram;
			std::map<uint64_t, uint64_t> sampler_maxanisotropy_histogram;
			std::map<std::string, uint64_t> sampler_unnormalized_coordinates_histogram;
			for (const cVkSampler& s : dev.samplers)
			{
				sampler_flags_histogram[VkSamplerCreateFlags_to_string(s.info.flags)]++;
				sampler_mag_filter_histogram[VkFilter_to_string(s.info.magFilter)]++;
				sampler_min_filter_histogram[VkFilter_to_string(s.info.minFilter)]++;
				sampler_mipmap_histogram[VkSamplerMipmapMode_to_string(s.info.mipmapMode)]++;
				sampler_address_mode_histogram[VkSamplerAddressMode_to_string(s.info.addressModeU)]++;
				sampler_address_mode_histogram[VkSamplerAddressMode_to_string(s.info.addressModeV)]++;
				sampler_address_mode_histogram[VkSamplerAddressMode_to_string(s.info.addressModeW)]++;
				sampler_anisotropy_histogram[s.info.anisotropyEnable ? "true" : "false"]++;
				sampler_maxanisotropy_histogram[s.info.maxAnisotropy]++;
				sampler_compareenable_histogram[s.info.compareEnable ? "true" : "false"]++;
				sampler_compareop_histogram[VkCompareOp_to_string(s.info.compareOp)]++;
				sampler_bordercolor_histogram[VkBorderColor_to_string(s.info.borderColor)]++;
				sampler_unnormalized_coordinates_histogram[s.info.unnormalizedCoordinates ? "true" : "false"]++;
			}
			dv["sampler_address_mode_histogram"] = histogram_to_json(sampler_address_mode_histogram);
			dv["sampler_flags_histogram"] = histogram_to_json(sampler_flags_histogram);
			dv["sampler_mag_filter_histogram"] = histogram_to_json(sampler_mag_filter_histogram);
			dv["sampler_min_filter_histogram"] = histogram_to_json(sampler_min_filter_histogram);
			dv["sampler_mipmap_histogram"] = histogram_to_json(sampler_mipmap_histogram);
			dv["sampler_compareop_histogram"] = histogram_to_json(sampler_compareop_histogram);
			dv["sampler_compareenable_histogram"] = histogram_to_json(sampler_compareenable_histogram);
			dv["sampler_bordercolor_histogram"] = histogram_to_json(sampler_bordercolor_histogram);
			dv["sampler_anisotropy_histogram"] = histogram_to_json(sampler_anisotropy_histogram);
			dv["sampler_maxanisotropy_histogram"] = histogram_to_json(sampler_maxanisotropy_histogram);
			dv["sampler_unnormalized_coordinates_histogram"] = histogram_to_json(sampler_unnormalized_coordinates_histogram);

			// Descriptorsets
			dv["descriptor_set_layouts_count"] = (Json::Value::UInt64)dev.descriptorSetLayouts.size();
			if (verbose >= 2)
			{
				dv["descriptor_set_layouts"] = Json::arrayValue;
				for (const cVkDescriptorSetLayout& layout : dev.descriptorSetLayouts)
				{
					Json::Value layoutv = json_base(layout);
					layoutv["flags"] = VkDescriptorSetLayoutCreateFlags_to_string(layout.flags);
					layoutv["bindings"] = Json::arrayValue;
					for (const cVkDescriptorSetLayoutBinding& binding : layout.bindings)
					{
						Json::Value bindingv; // _not_ based on cVkBase
						bindingv["binding"] = (unsigned)binding.binding;
						bindingv["type"] = VkDescriptorType_to_string(binding.descriptorType);
						bindingv["count"] = (unsigned)binding.descriptorCount;
						bindingv["flags"] = VkShaderStageFlags_to_string(binding.stageFlags);
						bindingv["immutable_samplers"] = (unsigned)binding.immutableSamplers.size();
						layoutv["bindings"].append(bindingv);
					}
					dv["descriptor_set_layouts"].append(layoutv);
				}
			}
			dv["renderpasses_count"] = (Json::Value::UInt64)dev.renderpasses.size();
			dv["renderpasses"] = Json::arrayValue;
			for (const cVkRenderPass& q : dev.renderpasses)
			{
				Json::Value pass = json_base(q);
				pass["attachments"] = (Json::Value::UInt64)q.attachments.size();
				pass["subpasses"] = q.subpassCount;
				pass["dependencies"] = q.dependencyCount;
				dv["renderpasses"].append(pass);
			}
			dv["descriptor_pools_count"] = (Json::Value::UInt64)dev.descriptorPools.size();
			dv["descriptor_pools"] = Json::arrayValue;
			std::map<uint64_t, uint64_t> set_sizes;
			std::map<uint64_t, uint64_t> pool_sizes;
			for (const cVkDescriptorPool& q : dev.descriptorPools)
			{
				Json::Value pool = json_base(q);
				pool["maxSets"] = q.maxSets;
				set_sizes[q.maxSets]++;
				pool_sizes[q.poolSizeCount]++;
				pool["poolSizeCount"] = q.poolSizeCount;
				pool["flags"] = VkDescriptorPoolCreateFlags_to_string(q.flags);
				append_if_relevant(dv["descriptor_pools"], pool, q);
			}
			dv["descriptor_pool_max_set_histogram"] = histogram_to_json(set_sizes);
			dv["descriptor_pool_size_histogram"] = histogram_to_json(pool_sizes);
			dv["framebuffers_count"] = (Json::Value::UInt64)dev.framebuffers.size();
			dv["framebuffers"] = Json::arrayValue;
			for (const cVkFramebuffer& q : dev.framebuffers)
			{
				Json::Value buf = json_base(q);
				buf["width"] = q.width;
				buf["height"] = q.height;
				buf["layers"] = q.layers;
				buf["flags"] = VkFramebufferCreateFlags_to_string(q.flags);
				// TBD attachments, renderPass
				append_if_relevant(dv["framebuffers"], buf, q);
			}
			dv["command_pools_count"] = (Json::Value::UInt64)dev.commandPools.size();
			dv["command_pools"] = Json::arrayValue;
			for (const cVkCommandPool& q : dev.commandPools)
			{
				Json::Value qv = json_base(q);
				std::map<uint64_t, uint64_t> call_histogram;
				std::map<std::string, uint64_t> counter_histogram;
				std::map<std::string, uint64_t> usageflag_histogram;
				std::map<std::string, uint64_t> stageflag_histogram;
				qv["command_buffers_count"] = (Json::Value::UInt64)q.commandBuffers.size();
				qv["command_buffers"] = Json::arrayValue;
				for (const cVkCommandBuffer& b : q.commandBuffers)
				{
					call_histogram[b.sum.metrics[0]]++;
					Json::Value buf = json_base(b);
					buf["commands"] = (Json::Value::UInt64)b.commands.size();
					buf["usage_flags"] = VkCommandBufferUsageFlags_to_string(b.flags);
					for (uint32_t i = 0; i < 32; i++) if ((1 << i) & b.flags) usageflag_histogram[VkCommandBufferUsageFlags_to_string(1 << i)]++;
					buf["stage_flags"] = VkPipelineStageFlags_to_string(b.flags);
					for (uint32_t i = 0; i < 32; i++) if ((1 << i) & b.maxStageFlags) stageflag_histogram[VkPipelineStageFlags_to_string(1 << i)]++;
					buf["times_called_lifetime"] = (Json::Value::UInt64)b.lifetime_calls;
					buf["times_called_frames_of_interest"] = (Json::Value::UInt64)b.frames_of_interest_calls;
					for (int i = 0; i < ENUM_MAX_COMMANDS; i++)
					{
						if (b.count[i].metrics[0] > 0)
						{
							std::string name = vk_command_to_string((vk_command)i);
							buf[name] = (Json::Value::UInt64)b.count[i].metrics[0];
							counter_histogram[name] += b.count[i].metrics[0];
						}
					}
					append_if_relevant(qv["command_buffers"], buf, b, 1);
				}
				qv["usage_flag_histogram"] = histogram_to_json(usageflag_histogram);
				qv["stage_flag_histogram"] = histogram_to_json(stageflag_histogram);
				qv["call_histogram"] = histogram_to_json(call_histogram);
				qv["counter_histogram"] = histogram_to_json(counter_histogram);
				dv["command_pools"].append(qv);
			}
			dv["queues"] = Json::arrayValue;
			for (const cVkQueue& q : dev.queues)
			{
				Json::Value qv = json_base(q);
				qv["index"] = q.index;
				qv["priority"] = q.priority;
				Json::Value stages;
				for (int i = 0; i < 32; i++)
				{
					unsigned bit = 1 << i;
					if (q.stageflag_usage.count(i) > 0) stages[VkPipelineStageFlags_to_string(bit)] = q.stageflag_usage.at(i);
				}
				qv["stage_flags"] = stages;
				dv["queues"].append(qv);
			}
			dv["shader_modules_count"] = (Json::Value::UInt64)dev.shaderModules.size();
			dv["shader_modules"] = Json::arrayValue;
			for (const cVkShaderModule& q : dev.shaderModules)
			{
				Json::Value qv = json_base(q);
				qv["vertices"] = (Json::Value::Int64)q.count.vertices;
				qv["draws"] = (Json::Value::Int64)q.count.draws;
				qv["dispatches"] = (Json::Value::Int64)q.count.dispatches;
				qv["flags"] = VkShaderModuleCreateFlags_to_string(q.flags);
				qv["pipelines"] = Json::arrayValue;
				for (int p : q.pipelines)
				{
					qv["pipelines"].append(p);
				}
				if (((dump_shaders == 1 && naive_intersect(q.used_in_frame, frames_of_interest)) || (dump_shaders == 2)) && (q.type != VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM))
				{
					if (q.used_as == 0)
					{
						fprintf(stderr, "Shader with uid: %d was not submitted to any queues. Skipping writeout.\n", q.uid);
						continue;
					}

					if (q.type != q.used_as)
					{
						fprintf(stderr, "Shader with uid: %d was used in multiple stages (type: %d, used_as: %d). Naming is likely not complete.\n", q.uid, static_cast<int>(q.type), static_cast<int>(q.used_as));
					}

					mkdir(report_name.c_str(), 0755);
					std::string shader_name_str = shader_name(q.type);
					std::string prefix = std::string(report_name);
					std::string filename = prefix + "/shader_i" + _to_string(instance->instance_id) + "_s" + _to_string(q.uid) + "." + shader_name_str + ".spv";
					qv["filename"] = filename;
					qv["type"] = shader_name_str;
					qv["frames_of_interest_used_in"] = Json::Value(Json::arrayValue);
					for (int frame_used : q.used_in_frame)
					{
						if (frames_of_interest.find(frame_used) != frames_of_interest.end())
						{
							qv["frames_of_interest_used_in"].append(frame_used);
						}
					}
					write_file(filename, q.code.data(), q.code.size());
				}
				if (naive_intersect(q.used_in_frame, frames_of_interest)) dv["shader_modules"].append(qv);
			}

			// Pipelines
			dv["pipelines_count"] = (Json::Value::UInt64)dev.pipelines.size();
			dv["pipelines"] = Json::arrayValue;
			std::map<std::string, uint64_t> pipeline_flags_histogram;
			for (const cVkPipeline& pipe : dev.pipelines)
			{
				pipeline_flags_histogram[VkPipelineCreateFlags_to_string(pipe.flags)]++;

				Json::Value p = json_base(pipe);
				p["flags"] = VkPipelineCreateFlags_to_string(pipe.flags);
				if (pipe.layout) p["layout"] = pipe.layout->uid;
				if (pipe.renderPass) p["renderpass"] = pipe.renderPass->uid;
				p["draws"] = (Json::Value::UInt64)pipe.count.draws;
				p["dispatches"] = (Json::Value::UInt64)pipe.count.dispatches;
				p["vertices"] = (Json::Value::UInt64)pipe.count.vertices;
				if (pipe.subpass != UINT32_MAX) p["subpass"] = pipe.subpass;
				p["stages"] = Json::arrayValue;
				for (const cVkPipelineStage &stage : pipe.stages)
				{
					Json::Value sv; // not based on cVkBase
					sv["flags"] = VkPipelineShaderStageCreateFlags_to_string(stage.flags);
					sv["stage"] = VkShaderStageFlags_to_string(stage.stage);
					if (stage.module)
					{
						sv["module"] = stage.module->uid;
					}
					sv["name"] = stage.name;
					sv["specialization_data"] = !stage.specializationData.empty();
					sv["specialization_map_count"] = (int)stage.specializationMap.size();
					p["stages"].append(sv);
				}
				if (pipe.vertexInputState.enabled)
				{
					p["vertex_input_state"] = Json::objectValue;
					p["vertex_input_state"]["flags"] = VkPipelineVertexInputStateCreateFlags_to_string(pipe.vertexInputState.flags);
					p["vertex_input_state"]["vertex_binding_descriptions_count"] = (int)pipe.vertexInputState.vertexBindingDescriptions.size();
					p["vertex_input_state"]["vertex_attribute_descriptions_count"] = (int)pipe.vertexInputState.vertexAttributeDescriptions.size();
				}
				if (pipe.vertexInputAssemblyState.enabled)
				{
					p["vertex_assembly_state"]["flags"] = VkPipelineInputAssemblyStateCreateFlags_to_string(pipe.vertexInputAssemblyState.flags);
					p["vertex_assembly_state"]["topology"] = VkPrimitiveTopology_to_string(pipe.vertexInputAssemblyState.topology);
					p["vertex_assembly_state"]["primitive_restart_enable"] = pipe.vertexInputAssemblyState.primitiveRestartEnable;
				}
				if (pipe.tessellationState.enabled)
				{
					p["tessellation_state"] = Json::objectValue;
					p["tessellation_state"]["flags"] = VkPipelineTessellationStateCreateFlags_to_string(pipe.tessellationState.flags);
					p["tessellation_state"]["patchControlPoints"] = pipe.tessellationState.patchControlPoints;
				}
				if (pipe.viewportState.enabled)
				{
					p["viewport_state"] = Json::objectValue;
					p["viewport_state"]["flags"] = VkPipelineViewportStateCreateFlags_to_string(pipe.viewportState.flags);
					p["viewport_state"]["viewports_count"] = (int)pipe.viewportState.viewports.size();
					p["viewport_state"]["scissors_count"] = (int)pipe.viewportState.scissors.size();
				}
				if (pipe.rasterizationState.enabled)
				{
					p["rasterization_state"] = Json::objectValue;
					p["rasterization_state"]["flags"] = VkPipelineRasterizationStateCreateFlags_to_string(pipe.rasterizationState.flags);
					p["rasterization_state"]["depthClampEnable"] = pipe.rasterizationState.depthClampEnable;
					p["rasterization_state"]["rasterizerDiscardEnable"] = pipe.rasterizationState.rasterizerDiscardEnable;
					p["rasterization_state"]["polygonMode"] = VkPolygonMode_to_string(pipe.rasterizationState.polygonMode);
					p["rasterization_state"]["cullMode"] = VkCullModeFlags_to_string(pipe.rasterizationState.cullMode);
					p["rasterization_state"]["frontFace"] = VkFrontFace_to_string(pipe.rasterizationState.frontFace);
					p["rasterization_state"]["depthBiasEnable"] = pipe.rasterizationState.depthBiasEnable;
					p["rasterization_state"]["depthBiasConstantFactor"] = pipe.rasterizationState.depthBiasConstantFactor;
					p["rasterization_state"]["depthBiasClamp"] = pipe.rasterizationState.depthBiasClamp;
					p["rasterization_state"]["depthBiasSlopeFactor"] = pipe.rasterizationState.depthBiasSlopeFactor;
					p["rasterization_state"]["lineWidth"] = pipe.rasterizationState.lineWidth;
				}
				if (pipe.multisampleState.enabled)
				{
					p["multisample_state"] = Json::objectValue;
					p["multisample_state"]["flags"] = VkPipelineMultisampleStateCreateFlags_to_string(pipe.multisampleState.flags);
					p["multisample_state"]["rasterizationSamples"] = pipe.multisampleState.rasterizationSamples;
					p["multisample_state"]["sampleShadingEnable"] = pipe.multisampleState.sampleShadingEnable;
					p["multisample_state"]["minSampleShading"] = pipe.multisampleState.minSampleShading;
					p["multisample_state"]["pSampleMask"] = "TBD"; // FIXME
					p["multisample_state"]["alphaToCoverageEnable"] = pipe.multisampleState.alphaToCoverageEnable;
					p["multisample_state"]["alphaToOneEnable"] = pipe.multisampleState.alphaToOneEnable;
				}
				if (pipe.pipelineColorBlendState.enabled)
				{
					p["pipeline_color_blend_state"] = Json::objectValue;
					p["pipeline_color_blend_state"]["flags"] = VkPipelineColorBlendStateCreateFlags_to_string(pipe.pipelineColorBlendState.flags);
					p["pipeline_color_blend_state"]["logicOpEnable"] = pipe.pipelineColorBlendState.logicOpEnable;
					p["pipeline_color_blend_state"]["logicOp"] = VkLogicOp_to_string(pipe.pipelineColorBlendState.logicOp);
					p["pipeline_color_blend_state"]["attachments_count"] = (int)pipe.pipelineColorBlendState.attachments.size();
					// TBD write out colorblendattachments
					p["pipeline_color_blend_state"]["blendConstants"] = Json::arrayValue;
					p["pipeline_color_blend_state"]["blendConstants"][0] = pipe.pipelineColorBlendState.blendConstants[0];
					p["pipeline_color_blend_state"]["blendConstants"][1] = pipe.pipelineColorBlendState.blendConstants[1];
					p["pipeline_color_blend_state"]["blendConstants"][2] = pipe.pipelineColorBlendState.blendConstants[2];
					p["pipeline_color_blend_state"]["blendConstants"][3] = pipe.pipelineColorBlendState.blendConstants[3];
				}
				if (pipe.dynamicState.enabled)
				{
					p["dynamic_state"] = Json::objectValue;
					p["dynamic_state"]["flags"] = VkPipelineDynamicStateCreateFlags_to_string(pipe.dynamicState.flags);
					p["dynamic_state"]["states_count"] = (int)pipe.dynamicState.states.size();
				}
				append_if_relevant(dv["pipelines"], p, pipe);
			}
			dv["pipeline_flags_histogram"] = histogram_to_json(pipeline_flags_histogram);

			pdv["devices"].append(dv);
		}
		result["physical_devices"].append(pdv);
	}
	Json::Value thread_histogram;
	for (const auto& pair : threads)
	{
		thread_histogram[_to_string(pair.first)] = (Json::Value::Int64)pair.second;
	}
	if (!deterministic) result["thread_resource_accesses"] = thread_histogram;

	std::string dump = result.toStyledString();
	std::string filename = report_name + "_" + _to_string(instance->instance_id) + ".json";
	write_file(filename, dump.c_str(), dump.size());

	std::string bufferfile = report_name + "_" + _to_string(instance->instance_id) + "_buffers.csv";
	FILE *fp = fopen(bufferfile.c_str(), "w");
	if (!fp)
	{
		fprintf(stderr, "Failed to open %s: %s", filename.c_str(), strerror(errno));
		return;
	}
	fprintf(fp, "Usage,Size,Offset\n");
	for (cVkPhysicalDevice& pdev : instance->GPUs)
	{
		for (cVkDevice& dev : pdev.devices)
		{
			for (const cVkBuffer& buf : dev.buffers)
			{
				fprintf(fp, "%s,%u,%u\n", VkBufferUsageFlags_to_string(buf.usage).c_str(), (unsigned)buf.size, (unsigned)buf.memoryOffset);
			}
		}
	}
	fclose(fp);
}
