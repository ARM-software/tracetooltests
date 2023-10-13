// Unit test to try out async vulkan compute
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_2.comp -o vulkan_compute_2.spirv
//   xxd -i vulkan_compute_2.spirv > vulkan_compute_2.inc
#include "vulkan_compute_2.inc"

#include <cmath>

static int queues = 2;
static int job_variant = 0;
static bool output = false;
static unsigned times = repeats();
static unsigned nodes = 10;
static vulkan_req_t req;
static int sync_variant = 0;

// these must also be changed in the shader
static int workgroup_size = 32;
static int width = 64;
static int height = 48;

struct resources
{
	std::vector<VkQueue> queues;
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	VkShaderModule computeShaderModule;
	VkCommandPool commandPool;
	VkDeviceMemory memory;
	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	std::vector<VkDescriptorSet> descriptorSet;
	std::vector<VkBuffer> buffer;
	std::vector<VkCommandBuffer> commandBuffer;
};

struct pixel
{
	float r, g, b, a;
};

static void show_usage()
{
	printf("-t/--times N           Times to repeat (default %u)\n", times);
	printf("-n/--nodes N           Job nodes to process (default %u)\n", nodes);
	printf("-q/--queues N          Set queues to use (default %d)\n", queues);
	printf("-j/--job-variant N     Set cross-job synchronization variant (default %d)\n", job_variant);
	printf("\t0 - synchronized with semaphores\n");
	printf("\t1 - no synchronization\n");
	printf("-s/--sync-variant N     Set final synchronization variant (default %d)\n", sync_variant);
	printf("\t0 - synchronized with vkDeviceWaitIdle\n");
	printf("\t1 - synchronized with vkQueueWaitIdlee\n");
	printf("\t2 - synchronized with fences\n");
	printf("-i/--image-output      Save an image of the output to disk\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-t", "--times"))
	{
		times = get_arg(argv, ++i, argc);
		return (times >= 1);
	}
	else if (match(argv[i], "-n", "--nodes"))
	{
		nodes = get_arg(argv, ++i, argc);
		return (nodes >= 1);
	}
	else if (match(argv[i], "-j", "--job-variant"))
	{
		job_variant = get_arg(argv, ++i, argc);
		return (job_variant >= 0 && job_variant <= 1);
	}
	else if (match(argv[i], "-s", "--sync-variant"))
	{
		sync_variant = get_arg(argv, ++i, argc);
		return (sync_variant >= 0 && sync_variant <= 2);
	}
	else if (match(argv[i], "-q", "--queues"))
	{
		queues = get_arg(argv, ++i, argc);
		req.queues = queues;
		return (queues >= 1);
	}
	else if (match(argv[i], "-i", "--image-output"))
	{
		output = true;
		return true;
	}
	return false;
}

void createComputePipeline(vulkan_setup_t& vulkan, resources& r)
{
	uint32_t code_size = long(ceil(vulkan_compute_2_spirv_len / 4.0)) * 4;
	std::vector<uint32_t> code(code_size);
	memcpy(code.data(), vulkan_compute_2_spirv, vulkan_compute_2_spirv_len);

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pCode = code.data();
	createInfo.codeSize = code_size;
	VkResult result = vkCreateShaderModule(vulkan.device, &createInfo, NULL, &r.computeShaderModule);
	check(result);

	VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {};
	shaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStageCreateInfo.module = r.computeShaderModule;
	shaderStageCreateInfo.pName = "main";

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = 1;
	pipelineLayoutCreateInfo.pSetLayouts = &r.descriptorSetLayout;
	result = vkCreatePipelineLayout(vulkan.device, &pipelineLayoutCreateInfo, NULL, &r.pipelineLayout);
	check(result);

        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = shaderStageCreateInfo;
        pipelineCreateInfo.layout = r.pipelineLayout;

	result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &r.pipeline);
	check(result);
}

int main(int argc, char** argv)
{
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.queues = queues;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_2", req);
	VkResult result;
	resources r;
	const size_t buffer_size = sizeof(pixel) * width * height;

	r.buffer.resize(nodes);
	r.commandBuffer.resize(nodes);
	r.descriptorSet.resize(nodes);

	r.queues.resize(queues);
	for (uint32_t i = 0; i < (unsigned)queues; i++) vkGetDeviceQueue(vulkan.device, 0, 0, &r.queues.at(i));

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	for (unsigned i = 0; i < nodes; i++)
	{
		result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &r.buffer.at(i));
		assert(result == VK_SUCCESS);
	}

	VkMemoryRequirements memory_requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, r.buffer.at(0), &memory_requirements);
	const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
	const uint32_t aligned_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo pAllocateMemInfo = {};
	pAllocateMemInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_size * nodes;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &r.memory);
	check(result);
	assert(r.memory != VK_NULL_HANDLE);

	VkDeviceSize offset = 0;
	for (unsigned i = 0; i < nodes; i++)
	{
		vkBindBufferMemory(vulkan.device, r.buffer.at(i), r.memory, offset);
		offset += aligned_size;
	}

	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
	descriptorSetLayoutBinding.binding = 0;
	descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorSetLayoutBinding.descriptorCount = 1;
	descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
	descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutCreateInfo.bindingCount = 1;
	descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;
        result = vkCreateDescriptorSetLayout(vulkan.device, &descriptorSetLayoutCreateInfo, nullptr, &r.descriptorSetLayout);
	check(result);

	VkDescriptorPoolSize descriptorPoolSize = {};
	descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorPoolSize.descriptorCount = nodes;
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
	descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCreateInfo.maxSets = nodes;
	descriptorPoolCreateInfo.poolSizeCount = 1;
	descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
	result = vkCreateDescriptorPool(vulkan.device, &descriptorPoolCreateInfo, nullptr, &r.descriptorPool);
	check(result);

	for (unsigned i = 0; i < nodes; i++)
	{
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
		descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocateInfo.descriptorPool = r.descriptorPool;
		descriptorSetAllocateInfo.descriptorSetCount = 1;
		descriptorSetAllocateInfo.pSetLayouts = &r.descriptorSetLayout;
		result = vkAllocateDescriptorSets(vulkan.device, &descriptorSetAllocateInfo, &r.descriptorSet.at(i));
		check(result);

		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = r.buffer.at(i);
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = buffer_size;
		VkWriteDescriptorSet writeDescriptorSet = {};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.dstSet = r.descriptorSet.at(i);
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
		vkUpdateDescriptorSets(vulkan.device, 1, &writeDescriptorSet, 0, NULL);
	}

	createComputePipeline(vulkan, r);

	VkCommandPoolCreateInfo commandPoolCreateInfo = {};
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.flags = 0;
	commandPoolCreateInfo.queueFamilyIndex = 0; // TBD fix
	result = vkCreateCommandPool(vulkan.device, &commandPoolCreateInfo, NULL, &r.commandPool);
	check(result);

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.commandPool = r.commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = nodes;
	result = vkAllocateCommandBuffers(vulkan.device, &commandBufferAllocateInfo, r.commandBuffer.data());
	check(result);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	for (unsigned i = 0; i < nodes; i++)
	{
		result = vkBeginCommandBuffer(r.commandBuffer.at(i), &beginInfo);
		check(result);
		vkCmdBindPipeline(r.commandBuffer.at(i), VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
		vkCmdBindDescriptorSets(r.commandBuffer.at(i), VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet.at(i), 0, NULL);
		vkCmdDispatch(r.commandBuffer.at(i), (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
		result = vkEndCommandBuffer(r.commandBuffer.at(i));
		check(result);
	}

	std::vector<VkFence> fences(nodes);
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = 0;
	for (unsigned i = 0; i < nodes; i++) result = vkCreateFence(vulkan.device, &fenceCreateInfo, NULL, &fences.at(i));
	check(result);

	for (unsigned i = 0; i < times; i++)
	{
		for (unsigned node = 0; node < nodes; node++)
		{
			VkPipelineStageFlags flag = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &r.commandBuffer.at(node);
			submit.pWaitDstStageMask = &flag;
			VkQueue queue = r.queues.at(node % queues);

			if (sync_variant == 2) result = vkQueueSubmit(queue, 1, &submit, fences.at(node));
			else result = vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);

			check(result);
		}
		if (sync_variant == 0) vkDeviceWaitIdle(vulkan.device);
		else if (sync_variant == 1) { for (VkQueue q : r.queues) vkQueueWaitIdle(q); }
		else if (sync_variant == 2)
		{
			result = vkWaitForFences(vulkan.device, nodes, fences.data(), VK_TRUE, UINT32_MAX);
			check(result);
			result = vkResetFences(vulkan.device, nodes, fences.data());
			check(result);
		}
	}

	// TBD : hash and verify each image by checksumming it

	if (output) test_save_image(vulkan, "mandelbrot.png", r.memory, 0, buffer_size, width, height);

	for (unsigned i = 0; i < nodes; i++) vkDestroyFence(vulkan.device, fences.at(i), NULL);
	for (unsigned i = 0; i < nodes; i++) vkDestroyBuffer(vulkan.device, r.buffer.at(i), NULL);
	testFreeMemory(vulkan, r.memory);
	vkDestroyShaderModule(vulkan.device, r.computeShaderModule, NULL);
	vkDestroyDescriptorPool(vulkan.device, r.descriptorPool, NULL);
	vkDestroyDescriptorSetLayout(vulkan.device, r.descriptorSetLayout, NULL);
	vkDestroyPipelineLayout(vulkan.device, r.pipelineLayout, NULL);
	vkDestroyPipeline(vulkan.device, r.pipeline, NULL);
	vkDestroyCommandPool(vulkan.device, r.commandPool, NULL);

	test_done(vulkan);
}
