// Inspired by https://github.com/jcupitt/opencl-experiments/blob/master/OpenCL_Hello_World_Example/hello.c

#include "opencl_common.h"
#include <inttypes.h>

static bool ugly_exit = false;
static opencl_req_t reqs;

static void show_usage()
{
	printf("-x/--ugly-exit         Exit without cleanup\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, opencl_req_t& reqs)
{
	if (match(argv[i], "-x", "--ugly-exit"))
	{
		ugly_exit = true;
		return true;
	}
	return false;
}

const char *source = "\n" \
"__kernel void square(                                                  \n" \
"   __global float* input,                                              \n" \
"   __global float* output,                                             \n" \
"   const unsigned int count)                                           \n" \
"{                                                                      \n" \
"   int i = get_global_id(0);                                           \n" \
"   if(i < count)                                                       \n" \
"       output[i] = input[i] * input[i];                                \n" \
"}                                                                      \n" \
"\n";

#define DATA_SIZE (1024)

int main(int argc, char** argv)
{
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	opencl_setup_t cl = cl_test_init(argc, argv, "opencl_general", reqs);
	int r;

	bench_start_iteration(cl.bench);

	cl_program program = clCreateProgramWithSource(cl.context, 1, (const char **)&source, NULL, &r);
	assert(program);

	r = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (r != CL_SUCCESS)
	{ // TBD move to common code
		size_t len;
		char buffer[2048];

		printf("Error: Failed to build program executable!\n");
		clGetProgramBuildInfo(program, cl.device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
		printf("%s\n", buffer);
		exit(1);
	}

	cl_kernel kernel = clCreateKernel(program, "square", &r);
	assert(kernel);
	assert(r == CL_SUCCESS);

	unsigned int count = DATA_SIZE;
	float data[DATA_SIZE];
	float results[DATA_SIZE];
	size_t global;
	size_t local;

	for (unsigned i = 0; i < count; i++) data[i] = rand() / (float)RAND_MAX;

	cl_mem input = clCreateBuffer(cl.context, CL_MEM_READ_ONLY,  sizeof(float) * count, NULL, NULL);
	cl_mem output = clCreateBuffer(cl.context, CL_MEM_WRITE_ONLY, sizeof(float) * count, NULL, NULL);
	assert(input);
	assert(output);

	r = clEnqueueWriteBuffer(cl.commands, input, CL_TRUE, 0, sizeof(float) * count, data, 0, NULL, NULL);
	assert(r == CL_SUCCESS);

	r  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
	assert(r == CL_SUCCESS);
	r = clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
	assert(r == CL_SUCCESS);
	r = clSetKernelArg(kernel, 2, sizeof(unsigned int), &count);
	assert(r == CL_SUCCESS);

	r = clGetKernelWorkGroupInfo(kernel, cl.device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
	assert(r == CL_SUCCESS);

	global = count;
	r = clEnqueueNDRangeKernel(cl.commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
	assert(r == CL_SUCCESS);

	clFinish(cl.commands);
	r = clEnqueueReadBuffer(cl.commands, output, CL_TRUE, 0, sizeof(float) * count, results, 0, NULL, NULL);
	assert(r == CL_SUCCESS);

	for (unsigned i = 0; i < count; i++)
	{
		assert(results[i] == data[i] * data[i]);
	}

	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseProgram(program);
	clReleaseKernel(kernel);

	bench_stop_iteration(cl.bench);

	// Optionally test ugly exit

	if (!ugly_exit)
	{
		cl_test_done(cl);
	}

	return 0;
}
