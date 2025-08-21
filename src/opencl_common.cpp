#include "opencl_common.h"
#include "external/json.hpp"
#include <fstream>

static void print_usage(const opencl_req_t& reqs)
{
	printf("Usage:\n");
	printf("-h/--help              This help\n");
	printf("-G/--gpu-native        Use the native GPU (default), fails if not available\n");
	printf("-C/--cpu-native        Use the native CPU, fails if not available\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default %d)\n", p__debug_level);
	if (reqs.usage) reqs.usage();
	exit(1);
}

static bool check_bench(opencl_setup_t& cl, opencl_req_t& reqs, const char* testname)
{
	const char* enable_json = getenv("BENCHMARKING_ENABLE_JSON");
	const char* enable_path = getenv("BENCHMARKING_ENABLE_FILE");
	char* content = nullptr;

	if (enable_path && enable_json) fprintf(stderr, "Both BENCHMARKING_ENABLE_JSON and BENCHMARKING_ENABLE_PATH are set -- this is an error!\n");

	if (enable_path)
	{
		printf("Reading benchmarking enable file: %s\n", enable_path);
		uint32_t size = 0;
		content = load_blob(enable_path, &size);
	}
	else if (enable_json)
	{
		printf("Reading benchmarking enable file directly from the environment variable\n");
		content = strdup(enable_json);
	}
	else return false;

	nlohmann::json data = nlohmann::json::parse(content);
	if (!data.count("target")) { printf("No app name in benchmarking enable file - skipping!\n"); return false; }
	if (data.value("target", "no target") != testname) { printf("Name in benchmarking enable file is not ours - skipping\n"); return false; }

	bench_init(cl.bench, testname, content, data.value("results", "results.json").c_str());

	return true;
}

void callback(const char* errinfo, const void* private_info, size_t cb, void* user_data)
{
	printf("Error from CL: %s\n", errinfo);
}

static std::string query_platform_string(cl_platform_id id, cl_platform_info param)
{
	size_t paramsize = 0;
	cl_int r = clGetPlatformInfo(id, param, 0, nullptr, &paramsize);
	assert(r == CL_SUCCESS);
	std::string str;
	str.resize(paramsize);
	r = clGetPlatformInfo(id, param, paramsize, str.data(), nullptr);
	assert(r == CL_SUCCESS);
	return str;
}

opencl_setup_t cl_test_init(int argc, char** argv, const std::string& testname, opencl_req_t& reqs)
{
	opencl_setup_t cl;
	bool force_native_gpu = false;
	bool force_native_cpu = false;

	// Parse bench enable file, if any
	check_bench(cl, reqs, testname.c_str());
	cl.bench.backend_name = "OpenCL"; // TBD + api;

	for (int i = 1; i < argc; i++)
	{
		if (match(argv[i], "-h", "--help"))
		{
			print_usage(reqs);
		}
		else if (match(argv[i], "-d", "--debug"))
		{
			p__debug_level = get_arg(argv, ++i, argc);
			if (p__debug_level > 3) print_usage(reqs);
		}
		else if (match(argv[i], "-G", "--gpu-native"))
		{
			force_native_gpu = true;
		}
		else if (match(argv[i], "-C", "--cpu-native"))
		{
			force_native_cpu = true;
		}
		else
		{
			if (!reqs.cmdopt || !reqs.cmdopt(i, argc, argv, reqs))
			{
				ELOG("Unrecognized or invalid cmd line parameter: %s", argv[i]);
				print_usage(reqs);
			}
		}
	}

	if (force_native_gpu && force_native_cpu)
	{
		ELOG("You cannot combine --gpu-native and --cpu-native, choose one!\n");
		print_usage(reqs);
	}

	cl_int r;
	cl_uint num_platforms;
	r = clGetPlatformIDs(0, nullptr, &num_platforms);
	if (r != CL_SUCCESS)
	{
		printf("Failed to query number of platforms: %s\n", errorString(r));
		exit(-1);
	}
	std::vector<cl_platform_id> platforms(num_platforms);
	r = clGetPlatformIDs(num_platforms, platforms.data(), nullptr);
	if (r != CL_SUCCESS)
	{
		printf("Failed to query platforms: %s\n", errorString(r));
		exit(-1);
	}
	printf("We found %d CL platforms\n", (int)num_platforms);

	for (auto platform : platforms)
	{
		std::string vendor = query_platform_string(platform, CL_PLATFORM_VENDOR);
		std::string version = query_platform_string(platform, CL_PLATFORM_VERSION);
		std::string profile = query_platform_string(platform, CL_PLATFORM_PROFILE);
		std::string name = query_platform_string(platform, CL_PLATFORM_NAME);
		printf("Platform %s by %s supporting %s with version %s\n", name.c_str(), vendor.c_str(), profile.c_str(), version.c_str());
	}

	// TBD actually use the above platform info somehow
	if (force_native_cpu)
	{
		cl_uint num_devices;
		r = clGetDeviceIDs(nullptr, CL_DEVICE_TYPE_CPU, 0, nullptr, &num_devices);
		assert(r == CL_SUCCESS);
		r = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_CPU, 1, &cl.device_id, NULL);
		assert(r == CL_SUCCESS);
		printf("We found %d CPU CL devices on the default platform\n", (int)num_devices);
	}
	else
	{
		cl_uint num_devices;
		r = clGetDeviceIDs(nullptr, CL_DEVICE_TYPE_GPU, 0, nullptr, &num_devices);
		assert(r == CL_SUCCESS);
		r = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_GPU, 1, &cl.device_id, NULL);
		assert(r == CL_SUCCESS);
		printf("We found %d GPU CL devices on the default platform\n", (int)num_devices);
	}

	cl.context = clCreateContext(0, 1, &cl.device_id, callback, NULL, &r);
	if (!cl.context)
	{
		printf("Failed to create a CL context: %s\n", errorString(r));
		exit(-1);
	}
#ifdef CL_VERSION_2_0
	cl.commands = clCreateCommandQueueWithProperties(cl.context, cl.device_id, nullptr, &r);
#else
	cl.commands = clCreateCommandQueue(cl.context, cl.device_id, 0, &r);
#endif
	if (!cl.commands)
	{
		printf("Failed to create CL command queue: %s\n", errorString(r));
	}

	return cl;
}

void cl_test_done(opencl_setup_t& cl, bool shared_instance)
{
	bench_done(cl.bench);

	int r = clReleaseCommandQueue(cl.commands);
	if (r != CL_SUCCESS) printf("Failed to release CL command queue: %s\n", errorString(r));
	r = clReleaseContext(cl.context);
	if (r != CL_SUCCESS) printf("Failed to release CL context: %s\n", errorString(r));
}

const char* errorString(const int errorCode)
{
	switch (errorCode)
	{
	case CL_SUCCESS: return "CL_SUCCESS";
	case CL_DEVICE_NOT_FOUND: return "CL_DEVICE_NOT_FOUND";
	case CL_DEVICE_NOT_AVAILABLE: return "CL_DEVICE_NOT_AVAILABLE";
	case CL_COMPILER_NOT_AVAILABLE: return "CL_COMPILER_NOT_AVAILABLE";
	case CL_MEM_OBJECT_ALLOCATION_FAILURE: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
	case CL_OUT_OF_RESOURCES: return "CL_OUT_OF_RESOURCES";
	case CL_OUT_OF_HOST_MEMORY: return "CL_OUT_OF_HOST_MEMORY";
	case CL_PROFILING_INFO_NOT_AVAILABLE: return "CL_PROFILING_INFO_NOT_AVAILABLE";
	case CL_MEM_COPY_OVERLAP: return "CL_MEM_COPY_OVERLAP";
	case CL_IMAGE_FORMAT_MISMATCH: return "CL_IMAGE_FORMAT_MISMATCH";
	case CL_IMAGE_FORMAT_NOT_SUPPORTED: return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
	case CL_BUILD_PROGRAM_FAILURE: return "CL_BUILD_PROGRAM_FAILURE";
	case CL_MAP_FAILURE: return "CL_MAP_FAILURE";
#ifdef CL_VERSION_1_1
	case CL_MISALIGNED_SUB_BUFFER_OFFSET: return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
	case CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST: return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
#endif
#ifdef CL_VERSION_1_2
	case CL_COMPILE_PROGRAM_FAILURE: return "CL_COMPILE_PROGRAM_FAILURE";
	case CL_LINKER_NOT_AVAILABLE: return "CL_LINKER_NOT_AVAILABLE";
	case CL_LINK_PROGRAM_FAILURE: return "CL_LINK_PROGRAM_FAILURE";
	case CL_DEVICE_PARTITION_FAILED: return "CL_DEVICE_PARTITION_FAILED";
	case CL_KERNEL_ARG_INFO_NOT_AVAILABLE: return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";
#endif
	case CL_INVALID_VALUE: return "CL_INVALID_VALUE";
	case CL_INVALID_DEVICE_TYPE: return "CL_INVALID_DEVICE_TYPE";
	case CL_INVALID_PLATFORM: return "CL_INVALID_PLATFORM";
	case CL_INVALID_DEVICE: return "CL_INVALID_DEVICE";
	case CL_INVALID_CONTEXT: return "CL_INVALID_CONTEXT";
	case CL_INVALID_QUEUE_PROPERTIES: return "CL_INVALID_QUEUE_PROPERTIES";
	case CL_INVALID_COMMAND_QUEUE: return "CL_INVALID_COMMAND_QUEUE";
	case CL_INVALID_HOST_PTR: return "CL_INVALID_HOST_PTR";
	case CL_INVALID_MEM_OBJECT: return "CL_INVALID_MEM_OBJECT";
	case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR: return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
	case CL_INVALID_IMAGE_SIZE: return "CL_INVALID_IMAGE_SIZE";
	case CL_INVALID_SAMPLER: return "CL_INVALID_SAMPLER";
	case CL_INVALID_BINARY: return "CL_INVALID_BINARY";
	case CL_INVALID_BUILD_OPTIONS: return "CL_INVALID_BUILD_OPTIONS";
	case CL_INVALID_PROGRAM: return "CL_INVALID_PROGRAM";
	case CL_INVALID_PROGRAM_EXECUTABLE: return "CL_INVALID_PROGRAM_EXECUTABLE";
	case CL_INVALID_KERNEL_NAME: return "CL_INVALID_KERNEL_NAME";
	case CL_INVALID_KERNEL_DEFINITION: return "CL_INVALID_KERNEL_DEFINITION";
	case CL_INVALID_KERNEL: return "CL_INVALID_KERNEL";
	case CL_INVALID_ARG_INDEX: return "CL_INVALID_ARG_INDEX";
	case CL_INVALID_ARG_VALUE: return "CL_INVALID_ARG_VALUE";
	case CL_INVALID_ARG_SIZE: return "CL_INVALID_ARG_SIZE";
	case CL_INVALID_KERNEL_ARGS: return "CL_INVALID_KERNEL_ARGS";
	case CL_INVALID_WORK_DIMENSION: return "CL_INVALID_WORK_DIMENSION";
	case CL_INVALID_WORK_GROUP_SIZE: return "CL_INVALID_WORK_GROUP_SIZE";
	case CL_INVALID_WORK_ITEM_SIZE: return "CL_INVALID_WORK_ITEM_SIZE";
	case CL_INVALID_GLOBAL_OFFSET: return "CL_INVALID_GLOBAL_OFFSET";
	case CL_INVALID_EVENT_WAIT_LIST: return "CL_INVALID_EVENT_WAIT_LIST";
	case CL_INVALID_EVENT: return "CL_INVALID_EVENT";
	case CL_INVALID_OPERATION: return "CL_INVALID_OPERATION";
	case CL_INVALID_GL_OBJECT: return "CL_INVALID_GL_OBJECT";
	case CL_INVALID_BUFFER_SIZE: return "CL_INVALID_BUFFER_SIZE";
	case CL_INVALID_MIP_LEVEL: return "CL_INVALID_MIP_LEVEL";
	case CL_INVALID_GLOBAL_WORK_SIZE: return "CL_INVALID_GLOBAL_WORK_SIZE";
#ifdef CL_VERSION_1_1
	case CL_INVALID_PROPERTY: return "CL_INVALID_PROPERTY";
#endif
#ifdef CL_VERSION_1_2
	case CL_INVALID_IMAGE_DESCRIPTOR: return "CL_INVALID_IMAGE_DESCRIPTOR";
	case CL_INVALID_COMPILER_OPTIONS: return "CL_INVALID_COMPILER_OPTIONS";
	case CL_INVALID_LINKER_OPTIONS: return "CL_INVALID_LINKER_OPTIONS";
	case CL_INVALID_DEVICE_PARTITION_COUNT: return "CL_INVALID_DEVICE_PARTITION_COUNT";
#endif
#ifdef CL_VERSION_2_0
	case CL_INVALID_PIPE_SIZE: return "CL_INVALID_PIPE_SIZE";
	case CL_INVALID_DEVICE_QUEUE: return "CL_INVALID_DEVICE_QUEUE";
#endif
#ifdef CL_VERSION_2_2
	case CL_INVALID_SPEC_ID: return "CL_INVALID_SPEC_ID";
	case CL_MAX_SIZE_RESTRICTION_EXCEEDED: return "CL_MAX_SIZE_RESTRICTION_EXCEEDED";
#endif
	}
	assert(false);
	return "Unknown CL error"; // we should never get here
}
