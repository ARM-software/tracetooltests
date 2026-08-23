#include <cassert>
#include <cstdio>
#include <cstring>

#include <CL/cl.h>
#include <CL/cl_ext.h>

static void check(cl_int result)
{
	if (result != CL_SUCCESS) fprintf(stderr, "OpenCL error: %d\n", result);
	assert(result == CL_SUCCESS);
}

static bool program_released = false;

static void CL_CALLBACK program_release_callback(cl_program, void*)
{
	program_released = true;
}

static void query_string(cl_platform_id platform, cl_platform_info property, char* output, size_t output_size)
{
	size_t required = 0;
	check(clGetPlatformInfo(platform, property, 0, nullptr, &required));
	assert(required <= output_size);
	check(clGetPlatformInfo(platform, property, output_size, output, nullptr));
}

static void query_string(cl_device_id device, cl_device_info property, char* output, size_t output_size)
{
	size_t required = 0;
	check(clGetDeviceInfo(device, property, 0, nullptr, &required));
	assert(required <= output_size);
	check(clGetDeviceInfo(device, property, output_size, output, nullptr));
}

int main()
{
	cl_uint platform_count = 0;
	check(clGetPlatformIDs(0, nullptr, &platform_count));
	assert(platform_count == 1);
	cl_platform_id platform = nullptr;
	check(clGetPlatformIDs(1, &platform, nullptr));

	char text[8192] = {};
	query_string(platform, CL_PLATFORM_NAME, text, sizeof(text));
	assert(strcmp(text, "ARM Platform") == 0);
	query_string(platform, CL_PLATFORM_EXTENSIONS, text, sizeof(text));
	assert(strstr(text, "cl_khr_icd"));

	cl_version platform_version = 0;
	check(clGetPlatformInfo(platform, CL_PLATFORM_NUMERIC_VERSION, sizeof(platform_version), &platform_version, nullptr));
	assert(CL_VERSION_MAJOR(platform_version) == 3);

	cl_uint device_count = 0;
	check(clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &device_count));
	assert(device_count == 1);
	device_count = 1;
	assert(clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 0, nullptr, &device_count) == CL_DEVICE_NOT_FOUND);
	assert(device_count == 0);
	cl_device_id device = nullptr;
	check(clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr));
	query_string(device, CL_DEVICE_NAME, text, sizeof(text));
	assert(strcmp(text, "Mali-G715 r0p0") == 0);
	query_string(device, CL_DEVICE_EXTENSIONS, text, sizeof(text));
	assert(strstr(text, "cl_khr_device_uuid"));
	assert(!strstr(text, "cl_khr_command_buffer"));
	assert(!clGetExtensionFunctionAddressForPlatform(platform, "clCreateCommandBufferKHR"));

	cl_uint compute_units = 0;
	check(clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, nullptr));
	assert(compute_units == 7);
	cl_uchar uuid[CL_UUID_SIZE_KHR] = {};
	check(clGetDeviceInfo(device, CL_DEVICE_UUID_KHR, sizeof(uuid), uuid, nullptr));
	assert(uuid[0] == 0x00 && uuid[1] == 0x00 && uuid[2] == 0xa2 && uuid[3] == 0xb8);
	cl_context context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, nullptr);
	assert(context);
	cl_uint format_count = 0;
	check(clGetSupportedImageFormats(context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D, 0, nullptr, &format_count));
	assert(format_count > 0);

	cl_int result = CL_SUCCESS;
	cl_command_queue queue = clCreateCommandQueueWithProperties(context, device, nullptr, &result);
	check(result);
	assert(queue);
	cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, 4, nullptr, &result);
	check(result);
	assert(buffer);
	cl_uint input = 0x12345678;
	cl_uint output = 0;
	cl_event event = nullptr;
	check(clEnqueueWriteBuffer(queue, buffer, CL_TRUE, 0, sizeof(input), &input, 0, nullptr, &event));
	assert(event);
	check(clWaitForEvents(1, &event));
	check(clReleaseEvent(event));
	check(clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, sizeof(output), &output, 0, nullptr, nullptr));
	assert(output == input);
	check(clReleaseMemObject(buffer));
	check(clReleaseCommandQueue(queue));
	const char* source = "__kernel void no_op() {}";
	cl_program program = clCreateProgramWithSource(context, 1, &source, nullptr, &result);
	check(result);
	assert(program);
	check(clSetProgramReleaseCallback(program, program_release_callback, nullptr));
	check(clReleaseProgram(program));
	assert(program_released);

	cl_image_format image_format = {CL_RGBA, CL_UNORM_INT8};
	cl_image_desc image_description = {};
	image_description.image_type = CL_MEM_OBJECT_IMAGE2D;
	image_description.image_width = 4;
	image_description.image_height = 4;
	cl_mem image = clCreateImage(context, CL_MEM_READ_WRITE, &image_format, &image_description, nullptr, &result);
	check(result);
	assert(image);
	check(clReleaseMemObject(image));
	check(clReleaseContext(context));

	return 0;
}
