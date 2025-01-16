#include "vulkan_graphics_common.h"

struct pixel
{
	float r, g, b, a;
};

void graphic_usage()
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

bool graphic_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
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

using namespace tracetooltests;

VkResult Buffer::create(VkBufferUsageFlags usage, VkDeviceSize size, VkSharingMode mode, VkMemoryPropertyFlags properties)
{
    m_createInfo.size = size;
    m_createInfo.usage = usage;
    m_createInfo.sharingMode = mode;

    VkResult result = vkCreateBuffer(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);

    {
    	VkMemoryRequirements memRequirements = {};
    	vkGetBufferMemoryRequirements(m_device, m_handle, &memRequirements);

    	const uint32_t alignMod = memRequirements.size % memRequirements.alignment;
    	const uint32_t alignedSize = (alignMod == 0) ? memRequirements.size : (memRequirements.size + memRequirements.alignment - alignMod);
    	const uint32_t memoryTypeIndex = get_device_memory_type(memRequirements.memoryTypeBits, properties);

    	VkMemoryAllocateInfo allocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    	allocateMemInfo.memoryTypeIndex = memoryTypeIndex;
    	allocateMemInfo.allocationSize = alignedSize;
    	VkMemoryAllocateFlagsInfo flaginfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };
    	if (req2.bufferDeviceAddress)
    	{
    		flaginfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    	}

    	if (vulkan2.apiVersion >= VK_API_VERSION_1_1)
    	{
    		allocateMemInfo.pNext = &flaginfo;
    	}
    	result = vkAllocateMemory(m_device, &allocateMemInfo, nullptr, &m_memory);
    	check(result);
    	assert(m_memory != VK_NULL_HANDLE);
    }

    {
    	if (vulkan2.apiVersion >= VK_API_VERSION_1_1)
    	{
    		VkBindBufferMemoryInfo bindBufferInfo = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, nullptr };
    		bindBufferInfo.buffer = m_handle;
    		bindBufferInfo.memory = m_memory;
    		bindBufferInfo.memoryOffset = 0;

    		result = vkBindBufferMemory2(m_device, 1, &bindBufferInfo);
    	}
    	else
    	{
    		result = vkBindBufferMemory(m_device, m_handle, m_memory, 0);
        }
        check(result);
    }

    return result;
}

VkResult Buffer::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult Image::create(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, uint32_t queueFamilyIndexCount /*= 0*/, uint32_t* queueFamilyIndex /*= nullptr*/, uint32_t mipLevels /*= 1*/, VkImageTiling tiling /*= VK_IMAGE_TILING_OPTIMAL*/)
{
    m_format = format;

	m_createInfo.flags = 0;
	m_createInfo.imageType = findImageType(extent);
	m_createInfo.format = format;
	m_createInfo.extent = extent;
	m_createInfo.mipLevels = mipLevels;
	m_createInfo.arrayLayers = 1;
	m_createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	m_createInfo.tiling = tiling;
	m_createInfo.usage = usage;
	m_createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	m_createInfo.queueFamilyIndexCount = queueFamilyIndexCount;
	m_createInfo.pQueueFamilyIndices = queueFamilyIndex;
	m_createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage(m_device, &m_createInfo, nullptr, &m_handle);
	check(result);

    {
    	VkMemoryRequirements memRequirements = {};
    	vkGetImageMemoryRequirements(m_device, m_handle, &memRequirements);

    	const uint32_t alignMod = memRequirements.size % memRequirements.alignment;
    	const uint32_t alignedSize = (alignMod == 0) ? memRequirements.size : (memRequirements.size + memRequirements.alignment - alignMod);

    	VkMemoryAllocateInfo allocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
    	allocateMemInfo.memoryTypeIndex = get_device_memory_type(memRequirements.memoryTypeBits, properties);;
    	allocateMemInfo.allocationSize = alignedSize;

    	VkMemoryAllocateFlagsInfo flaginfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };

    	if (req2.bufferDeviceAddress)
    	{
    		flaginfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    	}
    	if (vulkan2.apiVersion >= VK_API_VERSION_1_1)
    	{
    		allocateMemInfo.pNext = &flaginfo;
    	}

    	result = vkAllocateMemory(m_device, &allocateMemInfo, nullptr, &m_memory);
    	check(result);
    	assert(m_memory != VK_NULL_HANDLE);
    }

    {   
    	if (vulkan2.apiVersion >= VK_API_VERSION_1_1)
    	{
    		VkBindImageMemoryInfo bindImageInfo = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, nullptr };
    		bindImageInfo.image = m_handle;
    		bindImageInfo.memory = m_memory;
    		bindImageInfo.memoryOffset = 0;

    		result = vkBindImageMemory2(m_device, 1, &bindImageInfo);
    	}
    	else
    	{
    		result = vkBindImageMemory(m_device, m_handle, m_memory, 0);
        }
        check(result);
    }

    return result;
}

VkImageType Image::findImageType(VkExtent3D extent) const
{
    VkImageType dimension = VK_IMAGE_TYPE_2D;

    if (extent.depth > 1)       dimension = VK_IMAGE_TYPE_3D;
    else if (extent.height > 1) dimension = VK_IMAGE_TYPE_2D;
    else if (extent.width > 1)  dimension = VK_IMAGE_TYPE_1D;
    else assert(false);  // // should never happen

    return dimension;
}

VkResult Image::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

ImageView::ImageView(std::shared_ptr<Image> image)
    :m_pImage(image)
{
    m_device = image->m_device;
}

VkResult ImageView::create(VkImageViewType viewType, VkImageAspectFlags aspect)
{
    m_createInfo.image = m_pImage->getHandle();
    m_createInfo.viewType = viewType;
    m_createInfo.format = m_pImage->m_format;
    
    m_createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    
    m_createInfo.subresourceRange.aspectMask = aspect;
    m_createInfo.subresourceRange.baseMipLevel = 0;
    m_createInfo.subresourceRange.levelCount = 1;
    m_createInfo.subresourceRange.baseArrayLayer = 0;
    m_createInfo.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);

    return result;
}

VkResult ImageView::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult CommandBufferPool::create(VkCommandPoolCreateFlags flags, uint32_t queueFamilyIndex)
{
	m_createInfo.flags = flags;
	m_createInfo.queueFamilyIndex = queueFamilyIndex;

	VkResult result = vkCreateCommandPool(m_device, &m_createInfo, NULL, &m_handle);
	check(result);
    return result;
}

VkResult CommandBufferPool::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

CommandBuffer::CommandBuffer(std::shared_ptr<CommandBufferPool> commandBufferPool)
    :m_pCommandBufferPool(commandBufferPool)
{
    m_device = commandBufferPool->m_device;
}

VkResult CommandBuffer::create(VkCommandBufferLevel commandBufferLevel)
{
	m_createInfo.commandPool = m_pCommandBufferPool->getHandle();;
	m_createInfo.level = commandBufferLevel;
	m_createInfo.commandBufferCount = 1;

	VkResult result = vkAllocateCommandBuffers(m_device, &m_createInfo, &m_handle);
	check(result);

    return result;
}

VkResult CommandBuffer::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult RenderPass::create(const std::vector<AttachmentInfo>& attachments, const std::vector<SubpassInfo>& subpasses)
{
    m_attachmentDescriptions.resize(attachments.size());
    for (auto& attachment : attachments)
        m_attachmentDescriptions[attachment.m_location] = attachment.m_description;


    for (auto& subpass : subpasses)
    {
        VkSubpassDescription subDescription{};

        subDescription.flags = 0;
        subDescription.pipelineBindPoint = subpass.m_pipelineBindPoint;
        subDescription.inputAttachmentCount = static_cast<uint32_t>(subpass.m_inputAttachments.size());
        subDescription.pInputAttachments = subpass.m_inputAttachments.data();
        subDescription.colorAttachmentCount = static_cast<uint32_t>(subpass.m_colorAttachments.size());;
        subDescription.pColorAttachments = subpass.m_colorAttachments.data();
        subDescription.pResolveAttachments = subpass.m_resolveAttachments.data();
        subDescription.pDepthStencilAttachment =
            (subpass.m_depthAttachment.attachment != VK_ATTACHMENT_UNUSED)? &subpass.m_depthAttachment : nullptr;
        subDescription.preserveAttachmentCount = 0;
        subDescription.pPreserveAttachments = nullptr;
        m_subpassDescriptions.emplace_back(subDescription);
    }
    m_createInfo.attachmentCount = static_cast<uint32_t>(m_attachmentDescriptions.size());
    m_createInfo.pAttachments = m_attachmentDescriptions.data();
    m_createInfo.subpassCount = static_cast<uint32_t>(m_subpassDescriptions.size());;
    m_createInfo.pSubpasses = m_subpassDescriptions.data();
    m_createInfo.dependencyCount = 0;
    m_createInfo.pDependencies = nullptr; //&dependency;

    VkResult result = vkCreateRenderPass(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);

    return result;
}

VkResult RenderPass::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

graphic_resources graphic_init(vulkan_setup_t& vulkan, vulkan_req_t& reqs)
{
	graphic_resources r;
	VkResult result;

	vkGetDeviceQueue(vulkan.device, 0, 0, &r.queue);

	// set defaults if not overridden
	if (!reqs.options.count("width")) reqs.options["width"] = 640;
	if (!reqs.options.count("height")) reqs.options["height"] = 480;
	if (!reqs.options.count("wg_size")) reqs.options["wg_size"] = 32;

	const uint32_t width = std::get<int>(reqs.options.at("width"));
	const uint32_t height = std::get<int>(reqs.options.at("height"));
	r.buffer_size = sizeof(pixel) * width * height;

    auto commandPool = std::make_shared<CommandBufferPool>(vulkan.device);
    auto commandBufferFrameBoundary = std::make_shared<CommandBuffer>(commandPool);
    auto commandBufferDefault = std::make_shared<CommandBuffer>(commandPool);

    commandPool->create(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0);
    commandBufferFrameBoundary->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    commandBufferDefault->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	// Create an image for the frame boundary, in case we need it
    auto boundaryImage = std::make_shared<Image>(vulkan.device);
   	uint32_t queueFamilyIndex = 0;
    boundaryImage->create({width, height, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1, &queueFamilyIndex);


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

#if 0
void graphic_submit(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
{
	bench_start_scene(vulkan.bench, "graphics");
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

	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT32_MAX);
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

void graphics_done(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
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

void graphics_create_pipeline(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
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
#endif
