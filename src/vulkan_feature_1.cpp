#include "vulkan_common.h"

void usage()
{
}

int main()
{
	vulkan_req_t reqs;
	reqs.samplerAnisotropy = true; // request an unused feature
	vulkan_setup_t vulkan = test_init("vulkan_feature_1", reqs);
	test_done(vulkan); // that's it!
	return 0;
}
