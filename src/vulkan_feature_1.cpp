#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.samplerAnisotropy = true; // request an unused feature
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_feature_1", reqs);
	test_done(vulkan); // that's it!
	return 0;
}
