#pragma once

// This file contains Chameleon private data structures used
// for tracking Vulkan state.

#include "util.h"

#include <algorithm>
#include <vector>
#include <list> // need to use linked lists since we return pointers to contents _and_ add stuff
#include <string>
#include <map>
#include <unordered_set>
#include <vulkan/vk_icd.h>
#include <functional>
#include "vulkan_auto.h"

#include "json/json.h"


// -- Metrics definitions

struct MetricUnit
{
	long metrics[5];

	MetricUnit& operator+=(const MetricUnit& in) { for (int i = 0; i < 5; i++) metrics[i] += in.metrics[i]; return *this; }
	MetricUnit(long calls = 0, long _one = 0, long _two = 0, long _three = 0, long _four = 0) { metrics[0] = calls; metrics[1] = _one; metrics[2] = _two; metrics[3] = _three; metrics[4] = _four; }
};


// -- Vulkan internals definitions

// These should correspond to Vulkan public data type names, but prefixed with 'c'.

struct cVkDevice;
struct cVkSurfaceKHR;
struct cVkDisplayKHR;
struct cVkDisplayModeKHR;
struct cVkSubpass;
struct cVkPipeline;
struct cVkRenderPass;
struct cVkFramebuffer;
struct cVkPipelineCache;
struct cVkQueryPool;
struct cVkBuffer;
struct cVkDeviceMemory;
struct cVkDescriptorSet;
struct cVkSamplerYcbcrConversion;

struct cVkBase
{
	VK_LOADER_DATA loader_data; // required for dispatchable objects by the loader/ICD interface

	/// Object type
	VkObjectType object_type = VK_OBJECT_TYPE_UNKNOWN;
	/// Debug report object type (same as above, just different enum, why Khronos why do you do this to me??)
	VkDebugReportObjectTypeEXT debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT;
	/// Global frame counter
	static std::atomic_int current_frame;
	// Object lifetime tracking
	int created_frame = current_frame;
	int destroyed_frame = -1;
	/// Debug marker name
	std::string marker_name;
	// Debug marker tags
	const void* pTag = nullptr;
	size_t tagSize = 0;
	uint64_t tagName = 0;
	/// Log thread accesses
	std::unordered_set<long> accessed_by_thread;
	/// Log frame usage
	std::unordered_set<int> used_in_frame;
	/// Log frame usage - transitive
	std::unordered_set<int> used_in_frame_transitive;
	/// Unique ID for object for cross-object linking in dumped data
	static std::atomic_int last_uid;
	/// This object's UID
	int uid = last_uid++;
	/// We have no real deletion, so we can track everything
	bool destroyed = false;
	/// Private data
	std::map<VkPrivateDataSlot, uint64_t> slots;

	// This is used after the run to make sure parent usage is synced with child usage
	void update(cVkBase* parent)
	{
#ifndef FAST
		parent->used_in_frame_transitive.insert(used_in_frame_transitive.cbegin(), used_in_frame_transitive.cend());
#endif
	}

	// Helper function for user defined actions that should happen when touch(..) is called on this object
	// Can't use polymorphism here, as the vtable pointer will mess up the ICD_LOADER_MAGIC field on some compilers
	std::function<void()> log_usage;

	cVkBase()
	{
		set_loader_magic_value(this);
	}
};

struct cVkSamplerYcbcrConversion : cVkBase
{
};

struct cVkWeights : cVkBase
{
	cVkDeviceMemory* memory = nullptr;
	VkDeviceSize memoryOffset = 0;
};

struct cVkTensor : cVkBase
{
	cVkDeviceMemory* memory = nullptr;
	VkDeviceSize memoryOffset = 0;
};

#ifdef VK_ARM_SHADER_INSTRUMENTATION_SPEC_VERSION
struct cVkShaderInstrumentationARM : cVkBase
{
};
#endif

struct cVkPrivateDataSlot : cVkBase
{
};

struct cVkTensorView : cVkBase
{
};

struct cVkDataGraphPipelineSession : cVkBase
{
	cVkDeviceMemory* transientMemory = nullptr;
	VkDeviceSize transientMemoryOffset = 0;
	cVkDeviceMemory* statisticsMemory = nullptr;
	VkDeviceSize statisticsMemoryOffset = 0;
};

struct cVkPayload // _not_ based on cVkBase
{
	// nothing
};

struct cVkPayloadMarker : cVkPayload // _not_ based on VkBase
{
	std::string marker_name;

	cVkPayloadMarker(const std::string& _name) : marker_name(_name) {}
};

struct cVkPayloadQuery : cVkPayload // _not_ based on VkBase
{
	cVkQueryPool* queryPool = nullptr;
	uint32_t query = 0;
	VkQueryControlFlags flags = 0;
};

struct cVkPayloadCopyQuery : cVkPayload // _not_ based on VkBase
{
	cVkQueryPool* queryPool = nullptr;
	uint32_t firstQuery = 0;
	uint32_t queryCount = 0;
	cVkBuffer* dstBuffer = nullptr;
	VkDeviceSize dstOffset = 0;
	VkDeviceSize stride = 0;
	VkQueryControlFlags flags = 0;
};

struct cVkPayloadQueryReset : cVkPayload // _not_ based on VkBase
{
	uint32_t firstQuery = 0;
	uint32_t queryCount = 0;
};

struct cVkPayloadWriteAccelerationStructuresPropertiesKHR : cVkPayload
{
	uint32_t accelerationStructureCount = 0;
	uint32_t firstQuery = 0;
	std::vector<VkDeviceSize> sizes; // per-AS result payload (e.g. compacted sizes)
};

struct cVkPayloadWriteMicromapsPropertiesEXT : cVkPayload
{
	uint32_t micromapCount = 0;
	uint32_t firstQuery = 0;
};

struct cVkCommand // _not_ based on cVkBase
{
	/// Name of command (same as name of function called to set the command).
	std::vector<cVkBase*> bindings;
	/// Data payload
	cVkPayload* payload = nullptr;
	/// Lifetime counter
	MetricUnit count;
	/// Type of command
	vk_command name;

	cVkCommand(vk_command _name, MetricUnit _count) { name = _name; count = _count; }

	~cVkCommand() { delete payload; }
};

struct cVkCmdState // _not_ based on cVkBase
{
	cVkPipeline* pipeline = nullptr;
	cVkQueryPool* queryPool = nullptr;
	cVkDescriptorSet** descriptorSets = nullptr;
	uint32_t descriptorSetCount = 0;
	uint32_t query = 0;
};

struct cVkDescriptorUpdateTemplate : cVkBase
{
};

struct cVkCommandBuffer : cVkBase
{
	VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_MAX_ENUM;
	struct
	{
		cVkRenderPass* renderPass = nullptr;
		cVkFramebuffer* framebuffer = nullptr;
		VkBool32 occlusionQueryEnable = false;
		uint32_t subpass = 0;
		VkQueryControlFlags queryFlags = 0;
		VkQueryPipelineStatisticFlags pipelineStatistics = 0;
	} secondary;
	std::list<cVkCommand> commands;
	VkPipelineStageFlags2 maxStageFlags = 0;

	/// Breakdown of actions in the commandbuffer. Secondary command buffers are not counted.
	/// It is updated automatically in the ugly commandbuffer_command() macro
	std::vector<MetricUnit> count; ///< Number of times each command is called

	/// Sum of actions in the commandbuffer. Secondary command buffers are not counted.
	/// It is updated automatically in the ugly commandbuffer_command() macro
	MetricUnit sum;

	cVkPipeline* currently_bound_pipeline = nullptr;

	// We can multiply these values below with the values above to get lifetime and frame-of-interest
	// accumulated numbers.
	int lifetime_calls = 0;
	int frames_of_interest_calls = 0;

	VkCommandBufferUsageFlags flags;

	cVkCommandBuffer() : count(ENUM_MAX_COMMANDS)
	{
		object_type = VK_OBJECT_TYPE_COMMAND_BUFFER;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT;
	}
};

struct cVkCommandPool : cVkBase
{
	uint32_t queueFamilyIndex = 0;
	std::list<cVkCommandBuffer> commandBuffers;

	void update(cVkBase* parent);

	cVkCommandPool()
	{
		object_type = VK_OBJECT_TYPE_COMMAND_POOL;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT;
	}
};

struct cVkPhysicalDevice : cVkBase
{
	std::map<std::string, uint32_t> extensions;
	std::map<VkStructureType, std::pair<void*, size_t>> features;
	std::map<VkStructureType, std::pair<void*, size_t>> extendedProperties;
	VkPhysicalDeviceProperties properties = { 0 };
	VkPhysicalDeviceMemoryProperties memoryProperties = { 0 };
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
		nullptr,
		32,			// shaderGroupHandleSize
		1,			// maxRayRecursionDepth
		4096,			// maxShaderGroupStride
		64,			// shaderGroupBaseAlignment
		64,			// shaderGroupHandleCaptureReplaySize
		1073741824u,		// maxRayDispatchInvocationCount
		32,			// shaderGroupHandleAlignment
		32			// maxRayHitAttributeSize
	};
	VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
		nullptr,
		16777215,	// maxGeometryCount
		16777215,	// maxInstanceCount
		536870911,	// maxPrimitiveCount
		500000,		// maxPerStageDescriptorAccelerationStructures
		500000,		// maxPerStageDescriptorUpdateAfterBindAccelerationStructures
		500000,		// maxDescriptorSetAccelerationStructures
		500000,		// maxDescriptorSetUpdateAfterBindAccelerationStructures
		64		// minAccelerationStructureScratchOffsetAlignment
	};
	std::map<VkFormat, VkFormatProperties> formats;
	std::list<VkQueueFamilyProperties> queueFamilies;
	std::list<cVkDevice> devices;
	std::list<cVkDisplayKHR> displays;

	void update(cVkBase* parent);

	cVkPhysicalDevice()
	{
		object_type = VK_OBJECT_TYPE_PHYSICAL_DEVICE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT;
	}
};

struct cVkDebugReportCallbackEXT : cVkBase
{
	VkDebugReportFlagsEXT flags = VK_DEBUG_REPORT_FLAG_BITS_MAX_ENUM_EXT;
	PFN_vkDebugReportCallbackEXT callback;
	void* userdata = nullptr;
};

struct cVkInstance : cVkBase
{
	static std::atomic_int instance_counter;
	int instance_id = instance_counter;

	std::vector<std::string> enabledExtensions;
	std::list<cVkPhysicalDevice> GPUs; // found GPU definitions
	std::list<cVkSurfaceKHR> surfaces;
	std::list<cVkDisplayKHR> displays;
	std::list<cVkDebugReportCallbackEXT> callbacks;

	void update();

	cVkInstance()
	{
		instance_counter++;
		object_type = VK_OBJECT_TYPE_INSTANCE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT;
	}
};

static const std::vector<const char*> queue_counter_names {"QueueSubmitCalls", "QueueSubmits", "QueueSubmitFences",
	"QueueSubmitWaitSemaphores", "QueueSubmitSignalSemaphores", "WaitIdle"};

struct cVkQueue : cVkBase
{
	int index = -1;
	float priority = 0.0f;
	std::vector<cVkCommandBuffer*> commandbuffers;
	std::map<int, int> stageflag_usage; // mapping flag bit -> usage counter

	cVkQueue()
	{
		object_type = VK_OBJECT_TYPE_QUEUE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT;
	}
};

struct cVkDeviceMemory : cVkBase
{
	VkDeviceSize allocationSize = 0;
	uint32_t memoryTypeIndex = 0;
	char* ptr = nullptr;
	bool mapped = false;

	cVkDeviceMemory()
	{
		object_type = VK_OBJECT_TYPE_DEVICE_MEMORY;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT;
	}
};

struct cVkSurfaceKHR : cVkBase
{
	VkStructureType sType;
	VkSurfaceCapabilities2EXT capabilities = {
		.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES2_EXT,
		.pNext = nullptr,
		.minImageCount = 3,
		.maxImageCount = 3,
		.currentExtent = { .width = 1024, .height = 720 },
		.minImageExtent = { .width = 1024, .height = 720 },
		.maxImageExtent = { .width = 2048, .height = 2048 },
		.maxImageArrayLayers = 256,
		.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.supportedSurfaceCounters = 0, // TBD may support VK_SURFACE_COUNTER_VBLANK_EXT
	};
	cVkDisplayModeKHR* displayMode = nullptr;
	uint32_t planeIndex = 0;
	uint32_t planeStackIndex = 0;
	VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_FLAG_BITS_MAX_ENUM_KHR;
	float globalAlpha = 0.0f;
	VkDisplayPlaneAlphaFlagBitsKHR alphaMode = VK_DISPLAY_PLANE_ALPHA_FLAG_BITS_MAX_ENUM_KHR;
	VkExtent2D imageExtent = {};
	// values below are from nvidia desktop
	std::vector<VkSurfaceFormatKHR> formats { { .format = (VkFormat)44, .colorSpace = (VkColorSpaceKHR)0 }, { .format = (VkFormat)50, .colorSpace = (VkColorSpaceKHR)0 } }; // hack
	std::vector<VkPresentModeKHR> presentModes { VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR }; // hack

	cVkSurfaceKHR()
	{
		object_type = VK_OBJECT_TYPE_SURFACE_KHR;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT;
	}
};

struct cVkEvent : cVkBase
{
	VkEventCreateFlags flags;

	cVkEvent()
	{
		object_type = VK_OBJECT_TYPE_EVENT;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT;
	}
};

struct cVkFence : cVkBase
{
	VkFenceCreateFlags flags = 0;
	volatile bool signalled = false; // TBD this should be an atomic

	cVkFence()
	{
		object_type = VK_OBJECT_TYPE_FENCE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT;
	}
};

struct cVkSemaphore : cVkBase
{
	VkSemaphoreCreateFlags flags = 0;
	uint64_t value = 0;
	VkSemaphoreType type = VK_SEMAPHORE_TYPE_MAX_ENUM;

	cVkSemaphore()
	{
		object_type = VK_OBJECT_TYPE_SEMAPHORE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT;
	}
};

struct cVkImage : cVkBase
{
	VkImageCreateFlags flags = 0;
	VkImageType imageType = VK_IMAGE_TYPE_MAX_ENUM;
	VkFormat format = VK_FORMAT_MAX_ENUM;
	VkExtent3D extent {};
	uint32_t mipLevels = 0;
	uint32_t arrayLayers = 0;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
	VkImageTiling tiling = VK_IMAGE_TILING_MAX_ENUM;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;
	VkSharingMode sharingMode = VK_SHARING_MODE_MAX_ENUM;
	std::vector<uint32_t> queueFamilyIndices;
	VkImageLayout initialLayout = VK_IMAGE_LAYOUT_MAX_ENUM;
	cVkDeviceMemory* memory = nullptr;
	VkDeviceSize memoryOffset = 0;
	VkDeviceSize size = 0;

	cVkImage()
	{
		object_type = VK_OBJECT_TYPE_IMAGE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
	}
};

struct cVkImageView : cVkBase
{
	VkImageViewCreateFlags flags = 0;
	cVkImage* image = nullptr;
	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	VkFormat format = VK_FORMAT_MAX_ENUM;
	VkComponentMapping components {};
	VkImageSubresourceRange subresourceRange {};

	cVkImageView()
	{
		object_type = VK_OBJECT_TYPE_IMAGE_VIEW;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT;
	}
};

struct cVkBuffer : cVkBase
{
	VkBufferCreateFlags flags = 0;
	VkDeviceSize size = 0;
	VkBufferUsageFlags usage = 0;
	VkSharingMode sharingMode = VK_SHARING_MODE_MAX_ENUM;
	std::vector<uint32_t> queueFamilyIndices;
	cVkDeviceMemory* memory = nullptr;
	VkDeviceSize memoryOffset = 0;

	cVkBuffer()
	{
		object_type = VK_OBJECT_TYPE_BUFFER;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT;
	}
};

struct cVkBufferView : cVkBase
{
	VkBufferViewCreateFlags flags = 0;
	cVkBuffer* buffer = nullptr;
	VkFormat format = VK_FORMAT_MAX_ENUM;
	VkDeviceSize offset = 0;
	VkDeviceSize range = 0;

	cVkBufferView()
	{
		object_type = VK_OBJECT_TYPE_BUFFER_VIEW;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT;
	}
};

struct cVkQueryPool : cVkBase
{
	VkQueryPoolCreateFlags flags = 0;
	VkQueryType queryType = VK_QUERY_TYPE_MAX_ENUM;
	uint32_t queryCount = 0;
	VkQueryPipelineStatisticFlags pipelineStatistics = 0;
	std::vector<uint64_t> data;
	std::vector<bool> availability;
	int stride = 0;

	cVkQueryPool()
	{
		object_type = VK_OBJECT_TYPE_QUERY_POOL;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT;
	}
};

struct cVkShaderModule : cVkBase
{
	VkShaderModuleCreateFlags flags = 0;
	std::vector<char> code;
	VkShaderStageFlagBits type = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM; // set on use
	std::vector<int> pipelines; // where used
	VkShaderStageFlagBits used_as = (VkShaderStageFlagBits)0; // What it has been used for on submission (what stages)

	struct // from work it is involved with - a bit counterintuitive for frag shaders
	{
		long draws = 0;
		long vertices = 0;
		long dispatches = 0;
	} count;

	cVkShaderModule()
	{
		object_type = VK_OBJECT_TYPE_SHADER_MODULE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT;
	}
};

struct cVkSampler : cVkBase
{
	VkSamplerCreateInfo info {};

	cVkSampler()
	{
		object_type = VK_OBJECT_TYPE_SAMPLER;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT;
	}
};

struct cVkDescriptorSetLayoutBinding // _not_ based on cVkBase
{
	uint32_t binding = 0;
	VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
	uint32_t descriptorCount = 0;
	VkShaderStageFlags stageFlags = 0;

	std::vector<VkDescriptorImageInfo> pImageInfo;
	std::vector<VkDescriptorBufferInfo> pBufferInfo;
	std::vector<VkBufferView> pTexelBufferView;
	std::vector<VkSampler> immutableSamplers;

	void log_usage();
};

struct cVkDescriptorSetLayout : cVkBase
{
	VkDescriptorSetLayoutCreateFlags flags = 0;
	std::vector<cVkDescriptorSetLayoutBinding> bindings;

	cVkDescriptorSetLayout()
	{
		object_type = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT;
		log_usage = std::bind(&cVkDescriptorSetLayout::_log_usage, this);
	}

	void _log_usage()
	{
		for (auto& binding : bindings)
		{
			binding.log_usage();
		}
	}
};

struct cVkPipelineBinary : cVkBase
{
};

struct cVkPipelineLayout : cVkBase
{
	VkPipelineLayoutCreateFlags flags = 0;
	std::vector<cVkDescriptorSetLayout*> setLayouts;
	std::vector<VkPushConstantRange> pushConstantRanges;

	cVkPipelineLayout()
	{
		object_type = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT;
	}
};

struct cVkAttachmentDescription // _not_ based on cVkBase
{
	VkAttachmentDescription2 config;
};

struct cVkRenderPass : cVkBase
{
	std::vector<cVkAttachmentDescription> attachments;
	uint32_t subpassCount = 0;
	uint32_t dependencyCount = 0;

	cVkRenderPass()
	{
		object_type = VK_OBJECT_TYPE_RENDER_PASS;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT;
	}
};

struct cVkPipelineStage // _not_ based on cVkBase
{
	VkPipelineShaderStageCreateFlags flags = 0;
	VkShaderStageFlagBits stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	cVkShaderModule* module = nullptr;
	bool ownsModule = false;
	std::string name;
	std::vector<VkSpecializationMapEntry> specializationMap;
	std::vector<char> specializationData;

	void log_usage()
	{
		if (!module)
			return;
		module->used_as = static_cast<VkShaderStageFlagBits>(static_cast<int>(module->used_as) | static_cast<int>(stage));
	}
};

struct cVkPipeline : cVkBase
{
	VkPipelineCreateFlags flags = 0;
	cVkPipelineCache* cache = nullptr;
	std::vector<cVkPipelineStage> stages;
	struct
	{
		bool enabled = false;
		std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
		VkPipelineVertexInputStateCreateFlags flags = 0;
	} vertexInputState;
	struct
	{
		bool enabled = false;
		VkPipelineInputAssemblyStateCreateFlags flags = 0;
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
		bool primitiveRestartEnable = false;
	} vertexInputAssemblyState;
	struct
	{
		bool enabled = false;
		VkPipelineTessellationStateCreateFlags flags = 0;
		uint32_t patchControlPoints = 0;
	} tessellationState;
	struct
	{
		bool enabled = false;
		VkPipelineViewportStateCreateFlags flags = 0;
		std::vector<VkViewport> viewports;
		std::vector<VkRect2D> scissors;
	} viewportState;
	struct
	{
		bool enabled = false;
		VkPipelineRasterizationStateCreateFlags flags = 0;
		bool depthClampEnable = false;
		bool rasterizerDiscardEnable = false;
		VkPolygonMode polygonMode = VK_POLYGON_MODE_MAX_ENUM;
		VkCullModeFlags cullMode = VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
		VkFrontFace frontFace = VK_FRONT_FACE_MAX_ENUM;
		bool depthBiasEnable = false;
		float depthBiasConstantFactor = 0.0f;
		float depthBiasClamp = 0.0f;
		float depthBiasSlopeFactor = 0.0f;
		float lineWidth = 0.0f;
	} rasterizationState;
	struct
	{
		bool enabled = false;
		VkPipelineMultisampleStateCreateFlags flags = 0;
		VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
		bool sampleShadingEnable = false;
		float minSampleShading = 0.0f;
		//const VkSampleMask* pSampleMask;
		bool alphaToCoverageEnable = false;
		bool alphaToOneEnable = false;
	} multisampleState;
	struct
	{
		bool enabled = false;
		VkPipelineColorBlendStateCreateFlags flags = 0;
		bool logicOpEnable = false;
		VkLogicOp logicOp = VK_LOGIC_OP_MAX_ENUM;
		std::vector<VkPipelineColorBlendAttachmentState> attachments;
		float blendConstants[4];
	} pipelineColorBlendState;
	struct
	{
		bool enabled = false;
		VkPipelineDynamicStateCreateFlags flags;
		std::vector<VkDynamicState> states;
	} dynamicState;
	cVkPipelineLayout* layout = nullptr;
	cVkRenderPass* renderPass = nullptr;
	uint32_t subpass = UINT32_MAX;
	//TBD cVkPipeline* basePipelineHandle;
	//TBD int32_t basePipelineIndex;

	struct
	{
		long draws = 0;
		long vertices = 0;
		long dispatches = 0;
	} count;

	void update(cVkBase* parent);

	cVkPipeline()
	{
		object_type = VK_OBJECT_TYPE_PIPELINE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT;
		log_usage = std::bind(&cVkPipeline::_log_usage, this);
	}

	void _log_usage();
};

struct cVkPipelineCache : cVkBase
{
	VkPipelineCacheCreateFlags flags = 0;
	// TBD size_t initialDataSize;
	// TBD const void* pInitialData;

	/// pipeline in cache, bool if active or not (ie destroyed)
	std::map<cVkPipeline*, bool> pipelines;

	void update(cVkBase* parent);

	cVkPipelineCache()
	{
		object_type = VK_OBJECT_TYPE_PIPELINE_CACHE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT;
	}
};

struct cVkSubpass
{
	VkSubpassDescriptionFlags flags = VK_ATTACHMENT_DESCRIPTION_FLAG_BITS_MAX_ENUM;
	VkPipelineBindPoint pipelineBindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;
	std::vector<VkAttachmentReference> inputAttachments;
	std::vector<VkAttachmentReference> colorAttachments;
	std::vector<VkAttachmentReference> resolveAttachments;
	VkAttachmentReference depthStencilAttachment = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_MAX_ENUM };
	std::vector<uint32_t> preserveAttachments;
};

struct cVkDescriptorSet : cVkBase
{
	cVkDescriptorSetLayout* layout;
	std::vector<cVkDescriptorSetLayoutBinding> state_bindings;

	cVkDescriptorSet()
	{
		object_type = VK_OBJECT_TYPE_DESCRIPTOR_SET;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT;
	}

	void handle_write(const VkWriteDescriptorSet* descriptor_write)
	{
		for (cVkDescriptorSetLayoutBinding& layout_binding : state_bindings) {
			if (layout_binding.binding != descriptor_write->dstBinding) {
				continue;
			}

			switch (descriptor_write->descriptorType)
			{
			case VK_DESCRIPTOR_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_NV:
			case VK_DESCRIPTOR_TYPE_TENSOR_ARM:
				// TBD
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
				if (layout_binding.pImageInfo.size() < (descriptor_write->descriptorCount + descriptor_write->dstArrayElement))
				{
					layout_binding.pImageInfo.resize(descriptor_write->descriptorCount + descriptor_write->dstArrayElement);
					layout_binding.descriptorCount = layout_binding.pImageInfo.size();
				}
				for (unsigned j = 0; j < descriptor_write->descriptorCount; j++)
				{
					if (descriptor_write->pImageInfo[j].imageView == VK_NULL_HANDLE) continue;
					layout_binding.pImageInfo[j + descriptor_write->dstArrayElement] = descriptor_write->pImageInfo[j];
				}
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
				if (layout_binding.pBufferInfo.size() < (descriptor_write->descriptorCount + descriptor_write->dstArrayElement))
				{
					layout_binding.pBufferInfo.resize(descriptor_write->descriptorCount + descriptor_write->dstArrayElement);
					layout_binding.descriptorCount = layout_binding.pBufferInfo.size();
				}
				for (unsigned j = 0; j < descriptor_write->descriptorCount; j++)
				{
					if (descriptor_write->pBufferInfo[j].buffer == VK_NULL_HANDLE) continue;
					layout_binding.pBufferInfo[j + descriptor_write->dstArrayElement] = descriptor_write->pBufferInfo[j];
				}
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				if (layout_binding.pTexelBufferView.size() < (descriptor_write->descriptorCount + descriptor_write->dstArrayElement))
				{
					layout_binding.pTexelBufferView.resize(descriptor_write->descriptorCount + descriptor_write->dstArrayElement);
					layout_binding.descriptorCount = layout_binding.pTexelBufferView.size();
				}
				for (unsigned j = 0; j < descriptor_write->descriptorCount; j++)
				{
					if (descriptor_write->pTexelBufferView[j] == VK_NULL_HANDLE) continue;
					layout_binding.pTexelBufferView[j + descriptor_write->dstArrayElement] = descriptor_write->pTexelBufferView[j];
				}

				break;
			case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: // Provided by VK_VERSION_1_3
				break;
			case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: // Provided by VK_KHR_acceleration_structure
				break;
			case VK_DESCRIPTOR_TYPE_MUTABLE_EXT: // Provided by VK_EXT_mutable_descriptor_type
				break;
			case VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM: // Provided by VK_QCOM_image_processing
			case VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM: // Provided by VK_QCOM_image_processing
				break;
			case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV: // Provided by VK_NV_ray_tracing
				break;
			case VK_DESCRIPTOR_TYPE_MAX_ENUM:
				break;
			}

			break;
		}
	}
};

struct cVkDescriptorPool : cVkBase
{
	VkDescriptorPoolCreateFlags flags = 0;
	uint32_t maxSets = 0;
	uint32_t poolSizeCount = 0;
	std::list<cVkDescriptorSet> sets;

	void update(cVkBase* parent);

	cVkDescriptorPool()
	{
		object_type = VK_OBJECT_TYPE_DESCRIPTOR_POOL;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT;
	}
};

struct cVkFramebuffer : cVkBase
{
	VkFramebufferCreateFlags flags = 0;
	cVkRenderPass* renderPass = nullptr;
	std::vector<const cVkImageView*> attachments;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t layers = 0;

	cVkFramebuffer()
	{
		object_type = VK_OBJECT_TYPE_FRAMEBUFFER;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT;
	}
};

struct cVkDisplayModeKHR : cVkBase
{
	VkDisplayModeCreateFlagsKHR flags = 0;
	VkDisplayModeParametersKHR parameters{};

	cVkDisplayModeKHR()
	{
		object_type = VK_OBJECT_TYPE_DISPLAY_MODE_KHR;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_MODE_KHR_EXT;
	}
};

struct cVkDisplayKHR : cVkBase
{
	std::string displayName = "Default";
	VkExtent2D physicalDimensions = { 151, 80 };
	VkExtent2D physicalResolution = { 1920, 1080 };
	VkSurfaceTransformFlagsKHR supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	VkBool32 planeReorderPossible = false;
	VkBool32 persistentContent = false;
	std::list<cVkDisplayModeKHR> displayModes;

	void update(cVkBase* parent);

	cVkDisplayKHR()
	{
		// Create default display mode
		cVkDisplayModeKHR mode;
		mode.flags = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
		mode.parameters.visibleRegion = physicalResolution;
		mode.parameters.refreshRate = 60;
		displayModes.push_back(mode);
		object_type = VK_OBJECT_TYPE_DISPLAY_KHR;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_KHR_EXT;
	}
};

struct cVkSwapchainKHR : cVkBase
{
	cVkSurfaceKHR* surface = nullptr;
	uint32_t minImageCount = 0;
	VkFormat imageFormat = VK_FORMAT_MAX_ENUM;
	uint32_t queueFamilyIndexCount = 0;
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
	VkBool32 clipped = false;
	cVkSwapchainKHR* oldSwapchain = nullptr;
	VkExtent2D imageExtent {};
	uint32_t imageArrayLayers = 0;
	VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;
	VkSharingMode imageSharingMode = VK_SHARING_MODE_MAX_ENUM;

	/// Number of images in swapchain
	uint32_t imageCount = 3;
	/// Rotating current presentation image
	uint32_t currentImage = 0;
	/// Pointers to presentation images
	std::vector<cVkImage*> images;

	cVkSwapchainKHR()
	{
		object_type = VK_OBJECT_TYPE_SWAPCHAIN_KHR;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT;
	}
};

struct cVkAccelerationStructureKHR : cVkBase
{
	VkAccelerationStructureCreateFlagsKHR flags = 0;
	cVkBuffer* buffer = nullptr;
	VkDeviceSize memoryOffset = 0;
	VkDeviceSize memorySize = 0;
	VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR;

	cVkAccelerationStructureKHR()
	{
		object_type = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR_EXT;
	}
};

struct cVkMicromapEXT : cVkBase
{
};

struct cVkDevice : cVkBase
{
	std::list<cVkCommandPool> commandPools;
	std::list<cVkQueue> queues;
	std::vector<VkQueue> queue_ptrs; // quick lookup table
	std::list<cVkDeviceMemory> deviceMemory;
	std::list<cVkSwapchainKHR> swapchains;
	std::list<cVkFence> fences;
	std::list<cVkSemaphore> semaphores;
	std::list<cVkEvent> events;
	std::list<cVkImage> images;
	std::list<cVkImageView> imageViews;
	std::list<cVkBuffer> buffers;
	std::list<cVkBufferView> bufferViews;
	std::list<cVkQueryPool> queryPools;
	std::list<cVkShaderModule> shaderModules;
	std::list<cVkPipelineCache> pipelineCaches;
	std::list<cVkPipelineLayout> pipelineLayouts;
	std::list<cVkSampler> samplers;
	std::list<cVkDescriptorSetLayout> descriptorSetLayouts;
	std::list<cVkRenderPass> renderpasses;
	std::list<cVkDescriptorPool> descriptorPools;
	std::list<cVkFramebuffer> framebuffers;
	std::list<cVkPipeline> pipelines;
	std::list<cVkDescriptorUpdateTemplate> descriptorupdatetemplates;
	std::list<cVkAccelerationStructureKHR> accelerationStructures;
	std::list<cVkMicromapEXT> micromaps;
	std::list<cVkWeights> weights;
	std::list<cVkTensor> tensors;
#ifdef VK_ARM_SHADER_INSTRUMENTATION_SPEC_VERSION
	std::list<cVkShaderInstrumentationARM> shaderInstrumentations;
#endif
	std::list<cVkTensorView> tensorviews;
	std::list<cVkDataGraphPipelineSession> dataGraphPipelineSessions;
	std::list<cVkSamplerYcbcrConversion> samplerycbcrconversions;
	std::list<cVkPrivateDataSlot> slots;

	std::vector<std::string> enabledExtensions;

	void update(cVkBase* parent);

	uint32_t memoryTypeBits = 0;

	/// Amount of each type of memory that is currently allocated.
	std::map<uint32_t, uint64_t> memory_allocated;
	/// Highest amount of each type of memory that has ever been allocated.
	std::map<uint32_t, uint64_t> memory_highest;

	cVkDevice()
	{
		object_type = VK_OBJECT_TYPE_DEVICE;
		debug_object_type = VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT;
	}
};

#ifndef FAST
static inline void touch(cVkBase* v)
{
	// cVkShaderModule can be used after being destroyed, because
	// our graphics pipelines refer to them, but are supposed to
	// create some derived product off them instead.
	//assert(!v || !v->destroyed);
	if (v)
	{
		v->accessed_by_thread.insert(thread_id);
		v->used_in_frame.insert(v->current_frame);
		v->used_in_frame_transitive.insert(v->current_frame);
		if (v->log_usage)
		{
			v->log_usage();
		}
	}
}
#else
#define touch(v)
#endif

template<typename T, typename U>
static inline T* ccast(U api_type)
{
	T* v = reinterpret_cast<T*>(api_type);
	touch(v);
	return v;
}
