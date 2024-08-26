// Test that represent stupid apps that copy supported feature and extension lists to requested lists

#include "vulkan_common.h"

int main(int argc, char** argv)
{
	// First pass
	vulkan_req_t reqs;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_feature_2_init", reqs);
	// Copy all the offered extensions into the required list
	for (const auto& v : vulkan.instance_extensions) reqs.instance_extensions.push_back(v);
	for (const auto& v : vulkan.device_extensions) reqs.device_extensions.push_back(v);
	// Copy all the offered features into the required structs
	reqs.reqfeat13 = vulkan.hasfeat13;
	reqs.reqfeat12 = vulkan.hasfeat12;
	reqs.reqfeat11 = vulkan.hasfeat11;
	reqs.reqfeat2 = vulkan.hasfeat2;
	test_done(vulkan);

	// Second pass
	vulkan = test_init(argc, argv, "vulkan_feature_2_main", reqs);
	test_done(vulkan);
	return 0;
}
