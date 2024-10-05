#pragma once

#include "vulkan_common.h"

struct compute_resources
{
	VkQueue queue = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkShaderModule computeShaderModule = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkPipelineCache cache = VK_NULL_HANDLE;
	std::vector<uint32_t> code;
	int buffer_size = -1;

	// used for frame boundary extension
	VkImage image = VK_NULL_HANDLE;
	VkCommandBuffer commandBufferFrameBoundary = VK_NULL_HANDLE;
	int frame = 0;
};

bool compute_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs);
compute_resources compute_init(vulkan_setup_t& vulkan, vulkan_req_t& reqs);
void compute_done(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs);
void compute_submit(vulkan_setup_t& vulkan, compute_resources&  r, vulkan_req_t& reqs);
void compute_create_pipeline(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs);
void compute_usage();
