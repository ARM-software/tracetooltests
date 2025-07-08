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
	printf("-fb/--frame-boundary   Use frameboundary extension to publicize output\n");
	printf("-t/--times N           Times to repeat (default %d)\n", (int)p__loops);
}

bool compute_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-i", "--image-output"))
	{
		reqs.options["image_output"] = true;
		return true;
	}
	else if (match(argv[i], "-t", "--times"))
	{
		p__loops = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-pc", "--pipelinecache"))
	{
		reqs.options["pipelinecache"] = true;
		return true;
	}
	else if (match(argv[i], "-pcf", "--cachefile"))
	{
		reqs.options["cachefile"] = std::string(get_string_arg(argv, ++i, argc));
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
	else if (match(argv[i], "-fb", "--frame-boundary"))
	{
		return enable_frame_boundary(reqs);
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

	const uint32_t width = std::get<int>(reqs.options.at("width"));
	const uint32_t height = std::get<int>(reqs.options.at("height"));
	r.buffer_size = sizeof(pixel) * width * height;

	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = r.buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (reqs.options.count("frame_boundary"))
	{
		bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}
	if (reqs.bufferDeviceAddress)
	{
		bufferCreateInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
	}
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &r.buffer);
	assert(result == VK_SUCCESS);

	VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolCreateInfo.queueFamilyIndex = 0; // TBD fix
	result = vkCreateCommandPool(vulkan.device, &commandPoolCreateInfo, NULL, &r.commandPool);
	check(result);

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	commandBufferAllocateInfo.commandPool = r.commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &commandBufferAllocateInfo, &r.commandBuffer);
	check(result);
	result = vkAllocateCommandBuffers(vulkan.device, &commandBufferAllocateInfo, &r.commandBufferFrameBoundary);
	check(result);

	// Create an image for the frame boundary, in case we need it
	const uint32_t queueFamilyIndex = 0;
	VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	imageCreateInfo.flags = 0;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 1;
	imageCreateInfo.pQueueFamilyIndices = &queueFamilyIndex;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(vulkan.device, &imageCreateInfo, nullptr, &r.image);
	check(result);

	VkMemoryRequirements memory_requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, r.image, &memory_requirements);
	VkMemoryPropertyFlagBits memoryflags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, memoryflags);
	uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
	const uint32_t aligned_image_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);
	uint32_t total_size = aligned_image_size;

	vkGetBufferMemoryRequirements(vulkan.device, r.buffer, &memory_requirements);
	const uint32_t memoryTypeIndex2 = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	assert(memoryTypeIndex == memoryTypeIndex2); // else we're in trouble here
	align_mod = memory_requirements.size % memory_requirements.alignment;
	const uint32_t aligned_buffer_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);
	total_size += aligned_buffer_size;

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = total_size;
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

	if (vulkan.apiVersion >= VK_API_VERSION_1_1)
	{
		VkBindBufferMemoryInfo bindBufferInfo = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, nullptr };
		bindBufferInfo.buffer = r.buffer;
		bindBufferInfo.memory = r.memory;
		bindBufferInfo.memoryOffset = 0;

		result = vkBindBufferMemory2(vulkan.device, 1, &bindBufferInfo);
		check(result);

		VkBindImageMemoryInfo bindImageInfo = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, nullptr };
		bindImageInfo.image = r.image;
		bindImageInfo.memory = r.memory;
		bindImageInfo.memoryOffset = aligned_buffer_size; // comes after the buffer

		result = vkBindImageMemory2(vulkan.device, 1, &bindImageInfo);
		check(result);
	}
	else
	{
		result = vkBindBufferMemory(vulkan.device, r.buffer, r.memory, 0);
		check(result);

		result = vkBindImageMemory(vulkan.device, r.image, r.memory, aligned_buffer_size);
		check(result);
	}

	// Transition the image already to VK_IMAGE_LAYOUT_GENERAL
	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(r.commandBufferFrameBoundary, &beginInfo);
	check(result);
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
	barrier.srcAccessMask = VK_ACCESS_NONE;
	barrier.dstAccessMask = VK_ACCESS_NONE;
	barrier.image = r.image;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	vkCmdPipelineBarrier(r.commandBufferFrameBoundary, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	result = vkEndCommandBuffer(r.commandBufferFrameBoundary);
	check(result);
	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &r.commandBufferFrameBoundary;
	result = vkQueueSubmit(r.queue, 1, &submitInfo, VK_NULL_HANDLE);
	check(result);
	vkQueueWaitIdle(r.queue);

	return r;
}

void compute_submit(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
{
	bench_start_scene(vulkan.bench, "compute");
	bench_start_iteration(vulkan.bench);

	VkFence fence;
	VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	fenceCreateInfo.flags = 0;
	VkResult result = vkCreateFence(vulkan.device, &fenceCreateInfo, NULL, &fence);
	check(result);

	VkFrameBoundaryEXT fbinfo = { VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT, nullptr };
	fbinfo.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
	fbinfo.frameID = r.frame++;
	fbinfo.imageCount = 1;
	fbinfo.pImages = &r.image;
	fbinfo.bufferCount = 0;
	fbinfo.pBuffers = nullptr;

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	std::vector<VkCommandBuffer> cmdbufs;
	cmdbufs.push_back(r.commandBuffer);
	submitInfo.commandBufferCount = 1;
	if (reqs.options.count("frame_boundary"))
	{
		const uint32_t width = std::get<int>(reqs.options.at("width"));
		const uint32_t height = std::get<int>(reqs.options.at("height"));

		VkImageSubresourceLayers srlayer = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; // aspect mask, mip level, layer, layer count
		VkBufferImageCopy region = {};
		region.bufferOffset = 0;
		region.bufferRowLength = width; // in texels
		region.bufferImageHeight = height;
		region.imageSubresource = srlayer;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(r.commandBufferFrameBoundary, &beginInfo);
		check(result);
		vkCmdCopyBufferToImage(r.commandBufferFrameBoundary, r.buffer, r.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
		result = vkEndCommandBuffer(r.commandBufferFrameBoundary);
		check(result);
		submitInfo.commandBufferCount++;
		cmdbufs.push_back(r.commandBufferFrameBoundary);
		submitInfo.pNext = &fbinfo;
	}
	submitInfo.pCommandBuffers = cmdbufs.data();
	result = vkQueueSubmit(r.queue, 1, &submitInfo, fence);
	check(result);

	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	vkDestroyFence(vulkan.device, fence, nullptr);

	bench_stop_iteration(vulkan.bench);
	if (reqs.options.count("image_output"))
	{
		std::string filename = "compute_" + std::to_string(r.frame) + ".png";
		test_save_image(vulkan, filename.c_str(), r.memory, 0, std::get<int>(reqs.options.at("width")), std::get<int>(reqs.options.at("height")));
		bench_stop_scene(vulkan.bench, filename.c_str());
	}
	else bench_stop_scene(vulkan.bench);

	vkResetCommandBuffer(r.commandBuffer, 0);
}

void compute_done(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
{
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
	if (r.image) vkDestroyImage(vulkan.device, r.image, NULL);
	vkDestroyBuffer(vulkan.device, r.buffer, NULL);
	testFreeMemory(vulkan, r.memory);
	vkDestroyShaderModule(vulkan.device, r.computeShaderModule, NULL);
	vkDestroyDescriptorPool(vulkan.device, r.descriptorPool, NULL);
	vkDestroyDescriptorSetLayout(vulkan.device, r.descriptorSetLayout, NULL);
	vkDestroyPipelineLayout(vulkan.device, r.pipelineLayout, NULL);
	vkDestroyPipeline(vulkan.device, r.pipeline, NULL);
	vkDestroyCommandPool(vulkan.device, r.commandPool, NULL);
}

void compute_create_pipeline(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs, uint32_t pipeline_flags)
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
	pipelineCreateInfo.flags = pipeline_flags;

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
		check(result);
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
