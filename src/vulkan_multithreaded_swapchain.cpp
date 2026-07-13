#include "vulkan_window_common.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

static uint32_t frame_count = 600;
static uint32_t requested_images = 0;
static uint32_t timeout_seconds = 60;
static int requested_present_mode = -1;

struct frame_ticket
{
	uint32_t frame;
	uint32_t image_index;
	VkSemaphore acquire_semaphore;
};

struct ticket_queue
{
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<frame_ticket> tickets;
	bool producer_done = false;
	bool failed = false;
};

static void show_usage()
{
	printf("--frames N             Frames per variant (default %u)\n", frame_count);
	printf("--images 2|3|all       Requested swapchain image count (default all)\n");
	printf("--present MODE         Presentation mode: fifo, immediate, or all (default all)\n");
	printf("--timeout-seconds N    Deadlock timeout per variant (default %u)\n", timeout_seconds);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)reqs;
	if (match(argv[i], nullptr, "--frames"))
	{
		frame_count = get_arg(argv, ++i, argc);
		return frame_count > 0;
	}
	if (match(argv[i], nullptr, "--images"))
	{
		if (i + 1 >= argc) return false;
		const char* value = argv[++i];
		if (strcmp(value, "all") == 0) requested_images = 0;
		else requested_images = atoi(value);
		return requested_images == 0 || requested_images == 2 || requested_images == 3;
	}
	if (match(argv[i], nullptr, "--present"))
	{
		if (i + 1 >= argc) return false;
		const char* value = argv[++i];
		if (strcmp(value, "all") == 0) requested_present_mode = -1;
		else if (strcmp(value, "fifo") == 0) requested_present_mode = VK_PRESENT_MODE_FIFO_KHR;
		else if (strcmp(value, "immediate") == 0) requested_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		else return false;
		return true;
	}
	if (match(argv[i], nullptr, "--timeout-seconds"))
	{
		timeout_seconds = get_arg(argv, ++i, argc);
		return timeout_seconds > 0;
	}
	return false;
}

static VkCompositeAlphaFlagBitsKHR pick_composite_alpha(VkCompositeAlphaFlagsKHR supported)
{
	assert(supported != 0);
	if (supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
	if (supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
	assert(supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
	return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
}

static VkClearColorValue frame_color(uint32_t frame, uint32_t image_index, uint32_t image_variant)
{
	const uint32_t hash = frame * 2654435761u + image_index * 2246822519u + image_variant * 3266489917u;
	VkClearColorValue color{};
	color.float32[0] = static_cast<float>((hash >> 0) & 0xff) / 255.0f;
	color.float32[1] = static_cast<float>((hash >> 8) & 0xff) / 255.0f;
	color.float32[2] = static_cast<float>((hash >> 16) & 0xff) / 255.0f;
	color.float32[3] = 1.0f;
	return color;
}

static void clear_rect(VkCommandBuffer command_buffer, const VkClearColorValue& color, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
	VkClearAttachment attachment{};
	attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	attachment.colorAttachment = 0;
	attachment.clearValue.color = color;
	VkClearRect rect{};
	rect.rect.offset = { x, y };
	rect.rect.extent = { width, height };
	rect.baseArrayLayer = 0;
	rect.layerCount = 1;
	vkCmdClearAttachments(command_buffer, 1, &attachment, 1, &rect);
}

static void record_frame(VkCommandBuffer command_buffer, VkRenderPass render_pass, VkFramebuffer framebuffer, VkExtent2D extent,
	uint32_t frame, uint32_t image_index, uint32_t image_variant)
{
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VkResult result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkClearValue background{};
	background.color = frame_color(frame, image_index, image_variant);
	VkRenderPassBeginInfo render_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
	render_info.renderPass = render_pass;
	render_info.framebuffer = framebuffer;
	render_info.renderArea.extent = extent;
	render_info.clearValueCount = 1;
	render_info.pClearValues = &background;
	vkCmdBeginRenderPass(command_buffer, &render_info, VK_SUBPASS_CONTENTS_INLINE);

	VkClearColorValue image_color{};
	image_color.float32[image_index % 3] = 1.0f;
	image_color.float32[3] = 1.0f;
	const uint32_t stripe_height = std::max(1u, extent.height / 8);
	clear_rect(command_buffer, image_color, 0, 0, extent.width, stripe_height);

	VkClearColorValue bit_on = { { 1.0f, 1.0f, 1.0f, 1.0f } };
	VkClearColorValue bit_off = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	const uint32_t bit_width = std::max(1u, extent.width / 10);
	const uint32_t bit_height = std::max(1u, extent.height / 6);
	for (uint32_t bit = 0; bit < 10; ++bit)
	{
		const uint32_t x = bit * bit_width;
		const uint32_t width = (bit == 9) ? extent.width - x : bit_width;
		clear_rect(command_buffer, (frame & (1u << bit)) ? bit_on : bit_off, x, extent.height - bit_height, width, bit_height);
	}

	vkCmdEndRenderPass(command_buffer);
	result = vkEndCommandBuffer(command_buffer);
	check(result);
}

static bool contains_mode(const std::vector<VkPresentModeKHR>& modes, VkPresentModeKHR wanted)
{
	return std::find(modes.begin(), modes.end(), wanted) != modes.end();
}

static bool run_variant(vulkan_setup_t& vulkan, VkSurfaceKHR surface, const VkSurfaceCapabilitiesKHR& capabilities,
	VkSurfaceFormatKHR surface_format, VkPresentModeKHR present_mode, uint32_t image_variant)
{
	if (image_variant < capabilities.minImageCount || (capabilities.maxImageCount != 0 && image_variant > capabilities.maxImageCount))
	{
		printf("Skipping requested image count %u; supported range is %u..%u.\n", image_variant, capabilities.minImageCount, capabilities.maxImageCount);
		return false;
	}

	VkExtent2D extent = capabilities.currentExtent;
	if (extent.width == UINT32_MAX)
	{
		extent.width = std::max(capabilities.minImageExtent.width, std::min(400u, capabilities.maxImageExtent.width));
		extent.height = std::max(capabilities.minImageExtent.height, std::min(300u, capabilities.maxImageExtent.height));
	}

	VkSwapchainCreateInfoKHR swapchain_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr };
	swapchain_info.surface = surface;
	swapchain_info.minImageCount = image_variant;
	swapchain_info.imageFormat = surface_format.format;
	swapchain_info.imageColorSpace = surface_format.colorSpace;
	swapchain_info.imageExtent = extent;
	swapchain_info.imageArrayLayers = 1;
	swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_info.preTransform = capabilities.currentTransform;
	swapchain_info.compositeAlpha = pick_composite_alpha(capabilities.supportedCompositeAlpha);
	swapchain_info.presentMode = present_mode;
	swapchain_info.clipped = VK_TRUE;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkResult result = vkCreateSwapchainKHR(vulkan.device, &swapchain_info, nullptr, &swapchain);
	check(result);

	uint32_t image_count = 0;
	result = vkGetSwapchainImagesKHR(vulkan.device, swapchain, &image_count, nullptr);
	check(result);
	assert(image_count >= image_variant);
	std::vector<VkImage> images(image_count);
	result = vkGetSwapchainImagesKHR(vulkan.device, swapchain, &image_count, images.data());
	check(result);
	printf("Running %u frames: requested images=%u actual images=%u present=%s\n", frame_count, image_variant, image_count,
		present_mode == VK_PRESENT_MODE_FIFO_KHR ? "fifo" : "immediate");

	VkAttachmentDescription attachment{};
	attachment.format = surface_format.format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_reference;
	VkSubpassDependency dependency{};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	VkRenderPassCreateInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr };
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;
	VkRenderPass render_pass = VK_NULL_HANDLE;
	result = vkCreateRenderPass(vulkan.device, &render_pass_info, nullptr, &render_pass);
	check(result);

	std::vector<VkImageView> image_views(image_count);
	std::vector<VkFramebuffer> framebuffers(image_count);
	for (uint32_t i = 0; i < image_count; ++i)
	{
		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
		view_info.image = images[i];
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = surface_format.format;
		view_info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
		view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		result = vkCreateImageView(vulkan.device, &view_info, nullptr, &image_views[i]);
		check(result);
		VkFramebufferCreateInfo framebuffer_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr };
		framebuffer_info.renderPass = render_pass;
		framebuffer_info.attachmentCount = 1;
		framebuffer_info.pAttachments = &image_views[i];
		framebuffer_info.width = extent.width;
		framebuffer_info.height = extent.height;
		framebuffer_info.layers = 1;
		result = vkCreateFramebuffer(vulkan.device, &framebuffer_info, nullptr, &framebuffers[i]);
		check(result);
	}

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
	check(result);
	VkCommandBufferAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	allocate_info.commandPool = command_pool;
	allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &allocate_info, &command_buffer);
	check(result);

	VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	std::vector<VkSemaphore> acquire_semaphores(frame_count);
	std::vector<VkSemaphore> render_semaphores(image_count);
	for (auto& semaphore : acquire_semaphores)
	{
		result = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &semaphore);
		check(result);
	}
	for (auto& semaphore : render_semaphores)
	{
		result = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &semaphore);
		check(result);
	}
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence completion_fence = VK_NULL_HANDLE;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &completion_fence);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);
	ticket_queue work;
	std::mutex swapchain_mutex;
	std::mutex permit_mutex;
	std::condition_variable permit_condition;
	uint32_t acquire_permits = image_count - capabilities.minImageCount + 1;
	std::mutex watchdog_mutex;
	std::condition_variable watchdog_condition;
	bool variant_done = false;

	std::thread watchdog([&]() {
		std::unique_lock<std::mutex> lock(watchdog_mutex);
		if (!watchdog_condition.wait_for(lock, std::chrono::seconds(timeout_seconds), [&]() { return variant_done; }))
		{
			fprintf(stderr, "Timed out after %u seconds in requested-images=%u present=%s\n", timeout_seconds, image_variant,
				present_mode == VK_PRESENT_MODE_FIFO_KHR ? "fifo" : "immediate");
			fflush(stderr);
			std::_Exit(124);
		}
	});

	std::thread acquire_thread([&]() {
		for (uint32_t frame = 0; frame < frame_count; ++frame)
		{
			{
				std::unique_lock<std::mutex> lock(permit_mutex);
				permit_condition.wait(lock, [&]() { return acquire_permits > 0; });
				--acquire_permits;
			}
			uint32_t image_index = 0;
			VkResult acquire_result = VK_SUCCESS;
			{
				std::lock_guard<std::mutex> lock(swapchain_mutex);
				acquire_result = vkAcquireNextImageKHR(vulkan.device, swapchain, UINT64_MAX, acquire_semaphores[frame], VK_NULL_HANDLE, &image_index);
			}
			if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR)
			{
				{
					std::lock_guard<std::mutex> lock(permit_mutex);
					++acquire_permits;
				}
				permit_condition.notify_one();
				std::lock_guard<std::mutex> lock(work.mutex);
				work.failed = true;
				work.producer_done = true;
				work.condition.notify_all();
				return;
			}
			assert(image_index < image_count);
			{
				std::lock_guard<std::mutex> lock(work.mutex);
				work.tickets.push_back({ frame, image_index, acquire_semaphores[frame] });
			}
			work.condition.notify_one();
		}
		{
			std::lock_guard<std::mutex> lock(work.mutex);
			work.producer_done = true;
		}
		work.condition.notify_all();
	});

	std::thread submit_thread([&]() {
		VkResult submit_result = VK_SUCCESS;
		while (true)
		{
			frame_ticket ticket{};
			{
				std::unique_lock<std::mutex> lock(work.mutex);
				work.condition.wait(lock, [&]() { return !work.tickets.empty() || work.producer_done; });
				if (work.tickets.empty()) return;
				ticket = work.tickets.front();
				work.tickets.pop_front();
			}

			record_frame(command_buffer, render_pass, framebuffers[ticket.image_index], extent, ticket.frame, ticket.image_index, image_variant);
			VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
			submit_info.waitSemaphoreCount = 1;
			submit_info.pWaitSemaphores = &ticket.acquire_semaphore;
			submit_info.pWaitDstStageMask = &wait_stage;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffer;
			submit_info.signalSemaphoreCount = 1;
			submit_info.pSignalSemaphores = &render_semaphores[ticket.image_index];
			submit_result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
			check(submit_result);

			VkSubmitInfo empty_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
			submit_result = vkQueueSubmit(queue, 1, &empty_submit, completion_fence);
			check(submit_result);

			VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr };
			present_info.waitSemaphoreCount = 1;
			present_info.pWaitSemaphores = &render_semaphores[ticket.image_index];
			present_info.swapchainCount = 1;
			present_info.pSwapchains = &swapchain;
			present_info.pImageIndices = &ticket.image_index;
			{
				std::lock_guard<std::mutex> lock(swapchain_mutex);
				submit_result = vkQueuePresentKHR(queue, &present_info);
			}
			if (submit_result != VK_SUCCESS && submit_result != VK_SUBOPTIMAL_KHR)
			{
				std::lock_guard<std::mutex> lock(work.mutex);
				work.failed = true;
				return;
			}
			{
				std::lock_guard<std::mutex> lock(permit_mutex);
				++acquire_permits;
			}
			permit_condition.notify_one();

			submit_result = vkWaitForFences(vulkan.device, 1, &completion_fence, VK_TRUE, UINT64_MAX);
			check(submit_result);
			submit_result = vkResetFences(vulkan.device, 1, &completion_fence);
			check(submit_result);
			submit_result = vkResetCommandBuffer(command_buffer, 0);
			check(submit_result);
		}
	});

	acquire_thread.join();
	submit_thread.join();
	result = vkQueueWaitIdle(queue);
	check(result);
	{
		std::lock_guard<std::mutex> lock(watchdog_mutex);
		variant_done = true;
	}
	watchdog_condition.notify_one();
	watchdog.join();

	const bool success = !work.failed;
	vkDestroyFence(vulkan.device, completion_fence, nullptr);
	for (auto semaphore : render_semaphores) vkDestroySemaphore(vulkan.device, semaphore, nullptr);
	for (auto semaphore : acquire_semaphores) vkDestroySemaphore(vulkan.device, semaphore, nullptr);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	for (auto framebuffer : framebuffers) vkDestroyFramebuffer(vulkan.device, framebuffer, nullptr);
	for (auto view : image_views) vkDestroyImageView(vulkan.device, view, nullptr);
	vkDestroyRenderPass(vulkan.device, render_pass, nullptr);
	vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
	return success;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.surface = true;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	const char* winsys = getenv("TOOLSTEST_WINSYS");
	if (winsys && strcmp(winsys, "headless") == 0) reqs.instance_extensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
	else reqs.instance_extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_multithreaded_swapchain", reqs);

	testwindow window = test_window_create(vulkan, 0, 0, 400, 300);
	VkBool32 present_support = VK_FALSE;
	VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(vulkan.physical, 0, window.surface, &present_support);
	check(result);
	if (!present_support)
	{
		printf("Queue family 0 does not support presentation.\n");
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}

	VkSurfaceCapabilitiesKHR capabilities{};
	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan.physical, window.surface, &capabilities);
	check(result);
	if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
	{
		printf("Surface images do not support color attachments.\n");
		test_window_destroy(vulkan, window);
		test_done(vulkan);
		return 77;
	}

	uint32_t format_count = 0;
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, window.surface, &format_count, nullptr);
	check(result);
	assert(format_count > 0);
	std::vector<VkSurfaceFormatKHR> formats(format_count);
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical, window.surface, &format_count, formats.data());
	check(result);
	VkSurfaceFormatKHR surface_format = formats[0];
	if (format_count == 1 && surface_format.format == VK_FORMAT_UNDEFINED) surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;

	uint32_t mode_count = 0;
	result = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, window.surface, &mode_count, nullptr);
	check(result);
	assert(mode_count > 0);
	std::vector<VkPresentModeKHR> supported_modes(mode_count);
	result = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical, window.surface, &mode_count, supported_modes.data());
	check(result);
	assert(contains_mode(supported_modes, VK_PRESENT_MODE_FIFO_KHR));

	std::vector<uint32_t> image_variants;
	if (requested_images == 0) image_variants = { 2, 3 };
	else image_variants = { requested_images };
	std::vector<VkPresentModeKHR> present_variants;
	if (requested_present_mode == -1) present_variants = { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR };
	else present_variants = { static_cast<VkPresentModeKHR>(requested_present_mode) };

	uint32_t ran = 0;
	bool failed = false;
	bench_start_iteration(vulkan.bench);
	for (uint32_t images : image_variants)
	{
		for (VkPresentModeKHR mode : present_variants)
		{
			if (!contains_mode(supported_modes, mode))
			{
				printf("Skipping unsupported presentation mode %s.\n", mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" : "fifo");
				continue;
			}
			if (run_variant(vulkan, window.surface, capabilities, surface_format, mode, images)) ++ran;
			else if (images >= capabilities.minImageCount && (capabilities.maxImageCount == 0 || images <= capabilities.maxImageCount)) failed = true;
		}
	}
	bench_stop_iteration(vulkan.bench);

	test_window_destroy(vulkan, window);
	test_done(vulkan);
	if (failed) return 1;
	if (ran == 0) return 77;
	return 0;
}
