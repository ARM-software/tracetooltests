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
	reqs.device_extensions.push_back("VK_KHR_spirv_1_4"); // required for VK_EXT_mesh_shader
	reqs.device_extensions.push_back("VK_EXT_mesh_shader");
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_mesh", reqs);

	VkPhysicalDeviceMeshShaderFeaturesEXT meshfeats { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT, nullptr };
	VkPhysicalDeviceFeatures2 feat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &meshfeats };
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &feat2);
	printf("Mesh features supported:\n");
	printf("\ttaskShader = %s\n", meshfeats.taskShader ? "true" : "false");
	printf("\tmeshShader = %s\n", meshfeats.meshShader ? "true" : "false");
	printf("\tmultiviewMeshShader = %s\n", meshfeats.multiviewMeshShader ? "true" : "false");
	printf("\tprimitiveFragmentShadingRateMeshShader = %s\n", meshfeats.primitiveFragmentShadingRateMeshShader ? "true" : "false");
	printf("\tmeshShaderQueries = %s\n", meshfeats.meshShaderQueries ? "true" : "false");

	VkPhysicalDeviceMeshShaderPropertiesEXT meshprops { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceProperties2 props2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &meshprops };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &props2);
	printf("Mesh properties:\n");
	printf("\tmaxTaskWorkGroupTotalCount = %u\n", (unsigned)meshprops.maxTaskWorkGroupTotalCount);
	printf("\tmaxTaskWorkGroupCount = %u,%u,%u\n", (unsigned)meshprops.maxTaskWorkGroupCount[0], (unsigned)meshprops.maxTaskWorkGroupCount[1], (unsigned)meshprops.maxTaskWorkGroupCount[2]);
	printf("\tmaxTaskWorkGroupInvocations = %u\n", (unsigned)meshprops.maxTaskWorkGroupInvocations);
	printf("\tmaxTaskWorkGroupSize = %u,%u,%u\n", (unsigned)meshprops.maxTaskWorkGroupSize[0], (unsigned)meshprops.maxTaskWorkGroupSize[1], (unsigned)meshprops.maxTaskWorkGroupSize[2]);
	printf("\tmaxTaskPayloadSize = %u\n", (unsigned)meshprops.maxTaskPayloadSize);
	printf("\tmaxTaskSharedMemorySize = %u\n", (unsigned)meshprops.maxTaskSharedMemorySize);
	printf("\tmaxTaskPayloadAndSharedMemorySize = %u\n", (unsigned)meshprops.maxTaskPayloadAndSharedMemorySize);
	printf("\tmaxMeshWorkGroupTotalCount = %u\n", (unsigned)meshprops.maxMeshWorkGroupTotalCount);
	printf("\tmaxMeshWorkGroupCount = %u,%u,%u\n", (unsigned)meshprops.maxMeshWorkGroupCount[0], (unsigned)meshprops.maxMeshWorkGroupCount[1], (unsigned)meshprops.maxMeshWorkGroupCount[2]);
	printf("\tmaxMeshWorkGroupInvocations = %u\n", (unsigned)meshprops.maxMeshWorkGroupInvocations);
	printf("\tmaxMeshWorkGroupSize = %u,%u,%u\n", (unsigned)meshprops.maxMeshWorkGroupSize[0], (unsigned)meshprops.maxMeshWorkGroupSize[1], (unsigned)meshprops.maxMeshWorkGroupSize[2]);
	printf("\tmaxMeshSharedMemorySize = %u\n", (unsigned)meshprops.maxMeshSharedMemorySize);
	printf("\tmaxMeshPayloadAndSharedMemorySize = %u\n", (unsigned)meshprops.maxMeshPayloadAndSharedMemorySize);
	printf("\tmaxMeshOutputMemorySize = %u\n", (unsigned)meshprops.maxMeshOutputMemorySize);
	printf("\tmaxMeshPayloadAndOutputMemorySize = %u\n", (unsigned)meshprops.maxMeshPayloadAndOutputMemorySize);
	printf("\tmaxMeshOutputComponents = %u\n", (unsigned)meshprops.maxMeshOutputComponents);
	printf("\tmaxMeshOutputVertices = %u\n", (unsigned)meshprops.maxMeshOutputVertices);
	printf("\tmaxMeshOutputPrimitives = %u\n", (unsigned)meshprops.maxMeshOutputPrimitives);
	printf("\tmaxMeshOutputLayers = %u\n", (unsigned)meshprops.maxMeshOutputLayers);
	printf("\tmaxMeshMultiviewViewCount = %u\n", (unsigned)meshprops.maxMeshMultiviewViewCount);
	printf("\tmeshOutputPerVertexGranularity = %u\n", (unsigned)meshprops.meshOutputPerVertexGranularity);
	printf("\tmeshOutputPerPrimitiveGranularity = %u\n", (unsigned)meshprops.meshOutputPerPrimitiveGranularity);
	printf("\tmaxPreferredTaskWorkGroupInvocations = %u\n", (unsigned)meshprops.maxPreferredTaskWorkGroupInvocations);
	printf("\tmaxPreferredMeshWorkGroupInvocations = %u\n", (unsigned)meshprops.maxPreferredMeshWorkGroupInvocations);
	printf("\tprefersLocalInvocationVertexOutput = %u\n", (unsigned)meshprops.prefersLocalInvocationVertexOutput);
	printf("\tprefersLocalInvocationPrimitiveOutput = %u\n", (unsigned)meshprops.prefersLocalInvocationPrimitiveOutput);
	printf("\tprefersCompactVertexOutput = %u\n", (unsigned)meshprops.prefersCompactVertexOutput);
	printf("\tprefersCompactPrimitiveOutput = %u\n", (unsigned)meshprops.prefersCompactPrimitiveOutput);

	test_done(vulkan);
	return 0;
}
