#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.instance_extensions.push_back("VK_EXT_swapchain_colorspace"); // request an unused extension
	reqs.samplerAnisotropy = true; // request an unused feature
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_feature_1", reqs);
	test_done(vulkan); // that's it! now you can check your trace to see if the above appear in it
	return 0;
}
