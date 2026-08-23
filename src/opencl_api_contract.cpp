#define CL_USE_DEPRECATED_OPENCL_1_0_APIS
#include "opencl_common.h"

#include <cstring>
#include <vector>

static const char* kernel_source =
	"__kernel void no_op(__global uint* data) { data[get_global_id(0)] = 1; }";

int main(int argc, char** argv)
{
	opencl_req_t reqs{};
	reqs.minApiVersion = CL_MAKE_VERSION(3, 0, 0);
	opencl_setup_t cl = cl_test_init(argc, argv, "opencl_api_contract", reqs);
	bench_start_iteration(cl.bench);

	cl_int result = CL_SUCCESS;
	cl_uint host_storage[16] = {};
	cl_mem invalid_buffer = clCreateBuffer(cl.context, CL_MEM_READ_WRITE, sizeof(host_storage), host_storage, &result);
	assert(!invalid_buffer);
	assert(result == CL_INVALID_HOST_PTR);

	cl_mem buffer = clCreateBuffer(cl.context, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, sizeof(host_storage), host_storage, &result);
	assert(buffer);
	cl_check(result);
	void* queried_host_pointer = nullptr;
	result = clGetMemObjectInfo(buffer, CL_MEM_HOST_PTR, sizeof(queried_host_pointer), &queried_host_pointer, nullptr);
	cl_check(result);
	assert(queried_host_pointer == host_storage);
	void* mapped = clEnqueueMapBuffer(cl.commands, buffer, CL_TRUE, CL_MAP_WRITE, 0, sizeof(host_storage), 0, nullptr, nullptr, &result);
	assert(mapped == host_storage);
	cl_check(result);
	result = clEnqueueUnmapMemObject(cl.commands, buffer, mapped, 0, nullptr, nullptr);
	cl_check(result);

	cl_uint format_count = 0;
	result = clGetSupportedImageFormats(cl.context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D, 0, nullptr, &format_count);
	cl_check(result);
	if (!format_count)
	{
		bench_stop_iteration(cl.bench);
		result = clReleaseMemObject(buffer);
		cl_check(result);
		cl_test_done(cl);
		return 77;
	}
	std::vector<cl_image_format> formats(format_count);
	result = clGetSupportedImageFormats(cl.context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D,
		format_count, formats.data(), nullptr);
	cl_check(result);

	cl_image_desc image_description = {};
	image_description.image_type = CL_MEM_OBJECT_IMAGE2D;
	image_description.image_width = 4;
	image_description.image_height = 4;
	cl_mem image = clCreateImage(cl.context, CL_MEM_READ_WRITE, &formats[0], &image_description, nullptr, &result);
	assert(image);
	cl_check(result);
	size_t image_width = 0;
	result = clGetImageInfo(image, CL_IMAGE_WIDTH, sizeof(image_width), &image_width, nullptr);
	cl_check(result);
	assert(image_width == 4);

	size_t element_size = 0;
	result = clGetImageInfo(image, CL_IMAGE_ELEMENT_SIZE, sizeof(element_size), &element_size, nullptr);
	cl_check(result);
	std::vector<unsigned char> image_input(4 * 4 * element_size, 0x5a);
	std::vector<unsigned char> image_output(image_input.size(), 0);
	size_t origin[3] = {};
	size_t region[3] = {4, 4, 1};
	result = clEnqueueWriteImage(cl.commands, image, CL_TRUE, origin, region, 0, 0, image_input.data(), 0, nullptr, nullptr);
	cl_check(result);
	result = clEnqueueReadImage(cl.commands, image, CL_TRUE, origin, region, 0, 0, image_output.data(), 0, nullptr, nullptr);
	cl_check(result);
	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0) assert(image_output == image_input);

	cl_mem old_image = clCreateImage2D(cl.context, CL_MEM_READ_WRITE, &formats[0], 4, 4, 0, nullptr, &result);
	assert(old_image);
	cl_check(result);
	cl_sampler sampler = clCreateSampler(cl.context, CL_TRUE, CL_ADDRESS_CLAMP, CL_FILTER_NEAREST, &result);
	assert(sampler);
	cl_check(result);

	cl_command_queue_properties old_properties = 0;
	result = clSetCommandQueueProperty(cl.commands, CL_QUEUE_PROFILING_ENABLE, CL_TRUE, &old_properties);
	assert(result == CL_SUCCESS || result == CL_INVALID_OPERATION || result == CL_INVALID_QUEUE_PROPERTIES || result == CL_INVALID_VALUE);
	cl_event marker = nullptr;
	result = clEnqueueMarker(cl.commands, &marker);
	cl_check(result);
	assert(marker);
	result = clEnqueueWaitForEvents(cl.commands, 1, &marker);
	// PoCL leaves this deprecated entry point unimplemented.
	assert(result == CL_SUCCESS || result == CL_INVALID_OPERATION);
	result = clEnqueueBarrier(cl.commands);
	cl_check(result);

	cl_program program = clCreateProgramWithSource(cl.context, 1, &kernel_source, nullptr, &result);
	assert(program);
	cl_check(result);
	result = clBuildProgram(program, 0, nullptr, nullptr, nullptr, nullptr);
	cl_check(result);
	cl_kernel kernel = clCreateKernel(program, "no_op", &result);
	assert(kernel);
	cl_check(result);
	result = clSetKernelArg(kernel, 0, sizeof(buffer), &buffer);
	cl_check(result);
	cl_event task = nullptr;
	result = clEnqueueTask(cl.commands, kernel, 0, nullptr, &task);
	cl_check(result);
	assert(task);
	result = clFinish(cl.commands);
	cl_check(result);

	result = clReleaseEvent(task);
	cl_check(result);
	result = clReleaseKernel(kernel);
	cl_check(result);
	result = clReleaseProgram(program);
	cl_check(result);
	result = clReleaseEvent(marker);
	cl_check(result);
	result = clReleaseSampler(sampler);
	cl_check(result);
	result = clReleaseMemObject(old_image);
	cl_check(result);
	result = clReleaseMemObject(image);
	cl_check(result);
	result = clReleaseMemObject(buffer);
	cl_check(result);
	bench_stop_iteration(cl.bench);
	cl_test_done(cl);
	return 0;
}
