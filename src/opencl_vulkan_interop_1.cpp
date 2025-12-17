// Most basic OpenCL-Vulkan interop example possible

#include "vulkan_common.h"
#include "opencl_common.h"
#include <inttypes.h>

static opencl_req_t cl_reqs;
static vulkan_req_t vk_reqs;

int main(int argc, char** argv)
{
	vk_reqs.minApiVersion = VK_API_VERSION_1_1;
	vk_reqs.apiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "opencl_vulkan_interop_1", vk_reqs);

	// Get the UUID of the current Vulkan device
	VkPhysicalDeviceIDProperties physical_device_id_propreties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES, nullptr };
	VkPhysicalDeviceProperties2 physical_device_properties_2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &physical_device_id_propreties };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &physical_device_properties_2);

	static_assert(CL_UUID_SIZE_KHR == VK_UUID_SIZE);

	cl_reqs.device_by_uuid = physical_device_id_propreties.deviceUUID; // to match OpenCL device with Vulkan device
	cl_reqs.minApiVersion = CL_MAKE_VERSION(1, 2, 0);
	cl_reqs.extensions.push_back("cl_khr_device_uuid");
	// TBD: We discard the cmd line args for OpenCL here. We should do something smarter.
	opencl_setup_t cl = cl_test_init(1, argv, "opencl_vulkan_interop_1", cl_reqs);

	bench_start_iteration(vulkan.bench);

	// we do nothing

	bench_stop_iteration(vulkan.bench);

	cl_test_done(cl);
	test_done(vulkan);

	return 0;
}
