#include "vulkan_common.h"
#include <inttypes.h>

static vulkan_req_t reqs;

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

int main(int argc, char** argv)
{
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan1 = test_init(argc, argv, "vulkan_multidevice", reqs);
	reqs.instance = vulkan1.instance;
	vulkan_setup_t vulkan2 = test_init(argc, argv, "vulkan_multidevice", reqs);

	if (reqs.apiVersion >= VK_API_VERSION_1_1)
	{
		uint32_t devgrpcount = 0;
		VkResult r = vkEnumeratePhysicalDeviceGroups(vulkan1.instance, &devgrpcount, nullptr);
		check(r);
		std::vector<VkPhysicalDeviceGroupProperties> devgrps(devgrpcount);
		printf("Found %u physical device groups:\n", devgrpcount);
		r = vkEnumeratePhysicalDeviceGroups(vulkan1.instance, &devgrpcount, devgrps.data());
		for (auto& v : devgrps)
		{
			printf("\t%u devices (subsetAllocation=%s):", v.physicalDeviceCount, v.subsetAllocation ? "true" : "false");
			for (unsigned i = 0; i < v.physicalDeviceCount; i++) printf(" 0x%" PRIx64 ",", (uint64_t)v.physicalDevices[i]);
			printf("\n");
		}
	}

	test_done(vulkan2, true);
	test_done(vulkan1);

	return 0;
}
