// Unit test to try out vulkan compute with variations
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

static bool indirect = false;
static int indirectOffset = 0; // in units of indirect structs

struct pushconstants
{
	float width;
	float height;
};

static void show_usage()
{
	graphics_usage();
	printf("-I/--indirect          Use indirect compute dispatch (default %d)\n", (int)indirect);
	printf("  -ioff N              Use indirect compute dispatch buffer with this offset multiple (default %d)\n", indirectOffset);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-ioff", "--indirect-offset"))
	{
		indirectOffset = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-I", "--indirect"))
	{
		indirect = true;
		return true;
	}
	return graphics_cmdopt(i, argc, argv, reqs);
}

using namespace tracetooltests;

int main(int argc, char** argv)
{
	p__loops = 2; // default to 2 loops
	vulkan_req_t req;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_graphics_1", req);
	compute_resources r = graphic_init(vulkan, req);

	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	VkResult result;

    /******************************** render pass **********************************/
    // images used as attachments
    auto colorImage = std::make_shared<Image>(vulkan.device);
    colorImage->create({width, height, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto colorImageView = std::make_shared<ImageView>(colorImage);
    colorImageView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

    auto depthImage = std::make_shared<Image>(vulkan.device);
    depthImage->create({width, height, 1}, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto depthImageView = std::make_shared<ImageView>(depthImage);
    depthImageView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT);

    // attachments: set VkAttachmentDescription
    AttachmentInfo color{ 0, colorImageView, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    AttachmentInfo depth{ 1, depthImageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    // subpass: set VkAttachmentReference
    SubpassInfo subpass{};
    subpass.addColorAttachment(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    subpass.setDepthStencilAttachment(depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    // create renderpass
    auto renderpass = std::make_shared<RenderPass>(vulkan.device);
    renderpass->create({color,depth}, {subpass});

	test_done(vulkan);
}
