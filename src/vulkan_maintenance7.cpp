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
	reqs.device_extensions.push_back("VK_KHR_maintenance7");
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_maintenance7", reqs);

	bench_start_iteration(vulkan.bench);

	VkPhysicalDeviceLayeredApiPropertiesListKHR layerlist = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_LIST_KHR, nullptr };
	layerlist.layeredApiCount = 0; // to be filled out
	layerlist.pLayeredApis = nullptr;
	VkPhysicalDeviceProperties2 devprops = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &layerlist };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &devprops);
	if (layerlist.layeredApiCount == 0) printf("No layered APIs found!\n");
	else
	{
		std::vector<VkPhysicalDeviceLayeredApiPropertiesKHR> layerprops(layerlist.layeredApiCount);
		for (unsigned i = 0; i < layerlist.layeredApiCount; i++) layerprops[i] = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_KHR, nullptr };
		layerlist.pLayeredApis = layerprops.data();
		vkGetPhysicalDeviceProperties2(vulkan.physical, &devprops);
		for (unsigned i = 0; i < layerlist.layeredApiCount; i++)
		{
			printf("API layer %u: vendor=%u device=%u name=%s\n", i, layerprops[i].vendorID, layerprops[i].deviceID, layerprops[i].deviceName);
		}
	}

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
