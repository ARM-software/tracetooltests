#include "vulkan_compute_common.h"

struct pixel
{
	float r, g, b, a;
};

void compute_usage()
{
	printf("-i/--image-output      Save an image of the output to disk\n");
	printf("-W/--width             Width of output image (default 640)\n");
	printf("-H/--height            Height of output image (default 480)\n");
	printf("-wg/--workgroup-size   Set workgroup size (default 32)\n");
	printf("-pc/--pipelinecache    Add a pipeline cache to compute pipeline. By default it is empty.\n");
	printf("-pcf/--cachefile N     Save and restore pipeline cache to/from file N\n");
}

bool compute_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-i", "--image-output"))
	{
		reqs.options["image_output"] = 1;
		return true;
	}
	else if (match(argv[i], "-pc", "--pipelinecache"))
	{
		reqs.options["pipelinecache"] = 1;
		return true;
	}
	else if (match(argv[i], "-pcf", "--cachefile"))
	{
		reqs.options["cachefile"] = get_string_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-W", "--width"))
	{
		reqs.options["width"] = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-H", "--height"))
	{
		reqs.options["height"] = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-wg", "--workgroup-size"))
	{
		reqs.options["wg_size"] = get_arg(argv, ++i, argc);
		return true;
	}
	return false;
}

compute_resources compute_init(vulkan_setup_t& vulkan, vulkan_req_t& reqs)
{
	compute_resources r;
	VkResult result;

	vkGetDeviceQueue(vulkan.device, 0, 0, &r.queue);

	// set defaults if not overridden
	if (!reqs.options.count("width")) reqs.options["width"] = 640;
	if (!reqs.options.count("height")) reqs.options["height"] = 480;
	if (!reqs.options.count("wg_size")) reqs.options["wg_size"] = 32;

	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	r.buffer_size = sizeof(pixel) * width * height;

	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = r.buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (reqs.bufferDeviceAddress)
	{
		bufferCreateInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
	}
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &r.buffer);
	assert(result == VK_SUCCESS);

	VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	commandPoolCreateInfo.flags = 0;
	commandPoolCreateInfo.queueFamilyIndex = 0; // TBD fix
	result = vkCreateCommandPool(vulkan.device, &commandPoolCreateInfo, NULL, &r.commandPool);
	check(result);

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	commandBufferAllocateInfo.commandPool = r.commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &commandBufferAllocateInfo, &r.commandBuffer);
	check(result);

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan.device, r.buffer, &memory_requirements);
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
	const uint32_t aligned_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_size;
	VkMemoryAllocateFlagsInfo flaginfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };
	if (vulkan.apiVersion >= VK_API_VERSION_1_1)
	{
		pAllocateMemInfo.pNext = &flaginfo;
	}
	if (reqs.bufferDeviceAddress)
	{
		flaginfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
	}
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &r.memory);
	check(result);
	assert(r.memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, r.buffer, r.memory, 0);
	check(result);

	return r;
}

void compute_done(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
{
	if (reqs.options.count("image_output"))
	{
		test_save_image(vulkan, "compute.png", r.memory, 0, std::get<int>(reqs.options.at("width")), std::get<int>(reqs.options.at("height")));
	}

	if (reqs.options.count("pipelinecache") && reqs.options.count("cachefile"))
	{
		std::string file = std::get<std::string>(reqs.options.at("cachefile"));
		size_t size = 0;
		VkResult result = vkGetPipelineCacheData(vulkan.device, r.cache, &size, nullptr); // get size
		check(result);
		std::vector<char> blob(size);
		result = vkGetPipelineCacheData(vulkan.device, r.cache, &size, blob.data()); // get data
		check(result);
		save_blob(file, blob.data(), blob.size());
		ILOG("Saved pipeline cache data to %s", file.c_str());
		vkDestroyPipelineCache(vulkan.device, r.cache, nullptr);
	}
}

void compute_create_pipeline(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
{
	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	createInfo.pCode = r.code.data();
	createInfo.codeSize = r.code.size();
	VkResult result = vkCreateShaderModule(vulkan.device, &createInfo, NULL, &r.computeShaderModule);
	check(result);

	std::vector<VkSpecializationMapEntry> smentries(5);
	for (unsigned i = 0; i < smentries.size(); i++)
	{
		smentries[i].constantID = i;
		smentries[i].offset = i * 4;
		smentries[i].size = 4;
	}

	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int wg_size = std::get<int>(reqs.options.at("wg_size"));
	std::vector<int32_t> sdata(5);
	sdata[0] = wg_size; // workgroup x size
	sdata[1] = wg_size; // workgroup y size
	sdata[2] = 1; // workgroup z size
	sdata[3] = width; // surface width
	sdata[4] = height; // surface height

	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = smentries.size();
	specInfo.pMapEntries = smentries.data();
	specInfo.dataSize = sdata.size() * 4;
	specInfo.pData = sdata.data();

	VkPipelineShaderStageCreateInfo shaderStageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStageCreateInfo.module = r.computeShaderModule;
	shaderStageCreateInfo.pName = "main";
	shaderStageCreateInfo.pSpecializationInfo = &specInfo;

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipelineLayoutCreateInfo.setLayoutCount = 1;
	pipelineLayoutCreateInfo.pSetLayouts = &r.descriptorSetLayout;
	result = vkCreatePipelineLayout(vulkan.device, &pipelineLayoutCreateInfo, NULL, &r.pipelineLayout);
	check(result);

        VkComputePipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
        pipelineCreateInfo.stage = shaderStageCreateInfo;
        pipelineCreateInfo.layout = r.pipelineLayout;

	VkPipelineCreationFeedback creationfeedback = { 0, 0 };
	VkPipelineCreationFeedbackCreateInfo feedinfo = { VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO, nullptr, &creationfeedback, 0, nullptr };
	if (reqs.apiVersion == VK_API_VERSION_1_3)
	{
		pipelineCreateInfo.pNext = &feedinfo;
	}

	if (reqs.options.count("pipelinecache"))
	{
		char* blob = nullptr;
		VkPipelineCacheCreateInfo cacheinfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr };
		cacheinfo.flags = 0;
		if (reqs.options.count("cachefile") && exists_blob(std::get<std::string>(reqs.options.at("cachefile"))))
		{
			ILOG("Reading pipeline cache data from %s", std::get<std::string>(reqs.options.at("cachefile")).c_str());
			uint32_t size = 0;
			blob = load_blob(std::get<std::string>(reqs.options.at("cachefile")), &size);
			cacheinfo.initialDataSize = size;
			cacheinfo.pInitialData = blob;
		}
		result = vkCreatePipelineCache(vulkan.device, &cacheinfo, nullptr, &r.cache);
		free(blob);
	}

	result = vkCreateComputePipelines(vulkan.device, r.cache, 1, &pipelineCreateInfo, nullptr, &r.pipeline);
	check(result);

	if (reqs.apiVersion == VK_API_VERSION_1_3)
	{
		if (creationfeedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT)
		{
			ILOG("VkPipelineCreationFeedback value = %lu ns", (unsigned long)creationfeedback.duration);
		}
		else
		{
			ILOG("VkPipelineCreationFeedback invalid");
		}
	}
}
