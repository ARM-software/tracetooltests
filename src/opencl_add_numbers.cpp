// Test loosely based on https://github.com/rsnemmen/OpenCL-examples/tree/master/add_numbers

#include "opencl_common.h"

static const char* source = R"(
__kernel void add_numbers(__global float4* data,
                          __local float* local_result,
                          __global float* group_result)
{
    uint global_addr = get_global_id(0) * 2;
    float4 sum_vector = data[global_addr] + data[global_addr + 1];
    uint local_addr = get_local_id(0);
    local_result[local_addr] = sum_vector.s0 + sum_vector.s1 +
                               sum_vector.s2 + sum_vector.s3;
    barrier(CLK_LOCAL_MEM_FENCE);

    if (local_addr == 0)
    {
        float sum = 0.0f;
        for (uint i = 0; i < get_local_size(0); i++)
            sum += local_result[i];
        group_result[get_group_id(0)] = sum;
    }
}
)";

int main(int argc, char** argv)
{
	opencl_req_t reqs{};
	opencl_setup_t cl = cl_test_init(argc, argv, "opencl_add_numbers", reqs);
	bench_start_iteration(cl.bench);

	cl_int result;
	cl_program program = clCreateProgramWithSource(cl.context, 1, &source, nullptr, &result);
	assert(program);
	cl_check(result);
	result = clBuildProgram(program, 1, &cl.device_id, nullptr, nullptr, nullptr);
	if (result != CL_SUCCESS)
	{
		char log[4096] = {};
		cl_int log_result = clGetProgramBuildInfo(program, cl.device_id, CL_PROGRAM_BUILD_LOG,
		                                         sizeof(log), log, nullptr);
		cl_check(log_result);
		fprintf(stderr, "OpenCL build log:\n%s\n", log);
	}
	cl_check(result);

	cl_kernel kernel = clCreateKernel(program, "add_numbers", &result);
	assert(kernel);
	cl_check(result);

	const size_t global_size = 8;
	const size_t local_size = 4;
	size_t max_group_size = 0;
	result = clGetKernelWorkGroupInfo(kernel, cl.device_id, CL_KERNEL_WORK_GROUP_SIZE,
	                                  sizeof(max_group_size), &max_group_size, nullptr);
	cl_check(result);
	cl_ulong local_mem_size = 0;
	result = clGetDeviceInfo(cl.device_id, CL_DEVICE_LOCAL_MEM_SIZE,
	                         sizeof(local_mem_size), &local_mem_size, nullptr);
	cl_check(result);
	if (max_group_size < local_size || local_mem_size < local_size * sizeof(float))
	{
		result = clReleaseKernel(kernel);
		cl_check(result);
		result = clReleaseProgram(program);
		cl_check(result);
		bench_stop_iteration(cl.bench);
		cl_test_done(cl);
		return 77;
	}

	float data[64];
	for (unsigned i = 0; i < 64; i++) data[i] = static_cast<float>(i);
	float sums[2] = {};
	cl_mem input = clCreateBuffer(cl.context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
	                              sizeof(data), data, &result);
	assert(input);
	cl_check(result);
	cl_mem output = clCreateBuffer(cl.context, CL_MEM_WRITE_ONLY, sizeof(sums), nullptr, &result);
	assert(output);
	cl_check(result);

	result = clSetKernelArg(kernel, 0, sizeof(input), &input);
	cl_check(result);
	result = clSetKernelArg(kernel, 1, local_size * sizeof(float), nullptr);
	cl_check(result);
	result = clSetKernelArg(kernel, 2, sizeof(output), &output);
	cl_check(result);
	result = clEnqueueNDRangeKernel(cl.commands, kernel, 1, nullptr, &global_size,
	                                &local_size, 0, nullptr, nullptr);
	cl_check(result);
	result = clEnqueueReadBuffer(cl.commands, output, CL_TRUE, 0, sizeof(sums),
	                             sums, 0, nullptr, nullptr);
	cl_check(result);

	if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		assert(sums[0] == 496.0f);
		assert(sums[1] == 1520.0f);
		assert(sums[0] + sums[1] == 2016.0f);
	}

	result = clReleaseMemObject(output);
	cl_check(result);
	result = clReleaseMemObject(input);
	cl_check(result);
	result = clReleaseKernel(kernel);
	cl_check(result);
	result = clReleaseProgram(program);
	cl_check(result);
	bench_stop_iteration(cl.bench);
	cl_test_done(cl);
	return 0;
}
