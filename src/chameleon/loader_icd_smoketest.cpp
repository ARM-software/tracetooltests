#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

template<typename T>
static T load_symbol(void* library, const char* name)
{
	void* symbol = dlsym(library, name);
	if (!symbol)
	{
		fprintf(stderr, "Failed to load %s: %s\n", name, dlerror());
		exit(1);
	}
	return reinterpret_cast<T>(symbol);
}

static void check_vk(VkResult result, const char* message)
{
	if (result != VK_SUCCESS)
	{
		fprintf(stderr, "%s failed with %d\n", message, result);
		exit(1);
	}
}

int main()
{
	void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (!loader)
	{
		fprintf(stderr, "Failed to load the system Vulkan loader: %s\n", dlerror());
		return 1;
	}

	PFN_vkGetInstanceProcAddr get_instance_proc_addr = load_symbol<PFN_vkGetInstanceProcAddr>(loader, "vkGetInstanceProcAddr");
	PFN_vkCreateInstance create_instance = reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc_addr(VK_NULL_HANDLE, "vkCreateInstance"));
	PFN_vkEnumerateInstanceVersion enumerate_instance_version =
		reinterpret_cast<PFN_vkEnumerateInstanceVersion>(get_instance_proc_addr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
	if (!create_instance || !enumerate_instance_version)
	{
		fprintf(stderr, "Failed to query required global Vulkan entrypoints through the loader\n");
		dlclose(loader);
		return 1;
	}

	uint32_t api_version = 0;
	check_vk(enumerate_instance_version(&api_version), "vkEnumerateInstanceVersion");

	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
	app_info.apiVersion = api_version;
	VkInstanceCreateInfo create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
	create_info.pApplicationInfo = &app_info;

	VkInstance instance = VK_NULL_HANDLE;
	check_vk(create_instance(&create_info, nullptr, &instance), "vkCreateInstance");

	PFN_vkEnumeratePhysicalDevices enumerate_physical_devices =
		reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_instance_proc_addr(instance, "vkEnumeratePhysicalDevices"));
	PFN_vkDestroyInstance destroy_instance =
		reinterpret_cast<PFN_vkDestroyInstance>(get_instance_proc_addr(instance, "vkDestroyInstance"));
	if (!enumerate_physical_devices || !destroy_instance)
	{
		fprintf(stderr, "Failed to query required instance Vulkan entrypoints through the loader\n");
		dlclose(loader);
		return 1;
	}

	uint32_t physical_device_count = 0;
	check_vk(enumerate_physical_devices(instance, &physical_device_count, nullptr), "vkEnumeratePhysicalDevices");
	if (physical_device_count == 0)
	{
		fprintf(stderr, "Expected at least one Chameleon physical device\n");
		destroy_instance(instance, nullptr);
		dlclose(loader);
		return 1;
	}

	destroy_instance(instance, nullptr);
	dlclose(loader);
	return 0;
}
