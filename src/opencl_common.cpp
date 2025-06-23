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

	int r;
	if (force_native_cpu)
	{
		r = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_CPU, 1, &cl.device_id, NULL);
	}
	else
	{
		r = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_GPU, 1, &cl.device_id, NULL);
	}
	if (r != CL_SUCCESS)
	{
		printf("Failed to query CL devices!\n");
		exit(-1);
	}

	cl.context = clCreateContext(0, 1, &cl.device_id, callback, NULL, &r);
	if (!cl.context)
	{
		printf("Failed to create a CL context!\n");
		exit(-1);
	}
#ifdef CL_VERSION_2_0
	cl.commands = clCreateCommandQueueWithProperties(cl.context, cl.device_id, nullptr, &r);
#else
	cl.commands = clCreateCommandQueue(cl.context, cl.device_id, 0, &r);
#endif
	assert(cl.commands);

	return cl;
}

void cl_test_done(opencl_setup_t& cl, bool shared_instance)
{
	bench_done(cl.bench);

	clReleaseCommandQueue(cl.commands);
	clReleaseContext(cl.context);
}
