#pragma once

//
//  Header hacks
//

#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <vector>
#include <string>
#include <stdint.h>

/// Implement support for naming threads, missing from c++11
void set_thread_name(const char* name);

extern uint_fast8_t p__loops;
extern uint_fast8_t p__sanity;
extern uint_fast8_t p__debug_level;
extern uint_fast8_t p__validation;

#ifdef ANDROID
#include <sstream>
#include <android/log.h>
#include "android_utils.h"

#define OURNAME "TOOLSTEST"
#ifndef NDEBUG
#define DLOG3(_format, ...) do { if (p__debug_level >= 3) { ((void)__android_log_print(ANDROID_LOG_DEBUG, OURNAME, "%s:%d: " _format, __FILE__, __LINE__, ## __VA_ARGS__)); } } while(0)
#define DLOG2(_format, ...) do { if (p__debug_level >= 2) { ((void)__android_log_print(ANDROID_LOG_DEBUG, OURNAME, "%s:%d: " _format, __FILE__, __LINE__, ## __VA_ARGS__)); } } while(0)
#define DLOG(_format, ...) do { if (p__debug_level >= 1) { ((void)__android_log_print(ANDROID_LOG_DEBUG, OURNAME, "%s:%d: " _format, __FILE__, __LINE__, ## __VA_ARGS__)); } } while(0)
#else
#define DLOG3(_format, ...)
#define DLOG2(_format, ...)
#define DLOG(_format, ...)
#endif

#define ILOG(_format, ...) do { ((void)__android_log_print(ANDROID_LOG_INFO, OURNAME, "%s:%d: " _format, __FILE__, __LINE__, ## __VA_ARGS__)); } while(0)
#define WLOG(_format, ...) do { ((void)__android_log_print(ANDROID_LOG_WARN, OURNAME, "%s:%d: " _format, __FILE__, __LINE__, ## __VA_ARGS__)); } while(0)
#define ELOG(_format, ...) do { ((void)__android_log_print(ANDROID_LOG_ERROR, OURNAME, "%s:%d: " _format, __FILE__, __LINE__, ## __VA_ARGS__)); } while(0)
#define FELOG(_format, ...) do { ((void)__android_log_print(ANDROID_LOG_FATAL, OURNAME, "%s:%d: " _format, __FILE__, __LINE__, ## __VA_ARGS__)); } while(0)
#define ABORT(_format, ...) do { ((void)__android_log_print(ANDROID_LOG_FATAL, OURNAME, "%s:%d: " _format, __FILE__, __LINE__, ## __VA_ARGS__)); abort(); } while(0)

// Hack to workaround strange missing support for std::to_string in Android
template <typename T>
std::string _to_string(T value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

int STOI(const std::string& value);

#else // !ANDROID

static __attribute__((pure)) inline uint32_t adler32(unsigned char *data, size_t len)
{
	const uint32_t MOD_ADLER = 65521;
	uint32_t a = 1, b = 0;
	for (size_t index = 0; index < len; ++index)
	{
		a = (a + data[index]) % MOD_ADLER;
		b = (b + a) % MOD_ADLER;
	}
	return (b << 16) | a;
}

static __attribute__((pure)) inline uint64_t gettime()
{
	struct timespec t;
	// CLOCK_MONOTONIC_COARSE is much more light-weight, but resolution is quite poor.
	// CLOCK_PROCESS_CPUTIME_ID is another possibility, it ignores rest of system, but costs more,
	// and also on some CPUs process migration between cores can screw up such measurements.
	// CLOCK_MONOTONIC is therefore a reasonable and portable compromise.
	clock_gettime(CLOCK_MONOTONIC, &t);
	return ((uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec);
}

#ifndef NDEBUG
/// Using DLOGn() instead of DLOG(n,...) so that we can conditionally compile without some of them
#define DLOG3(_format, ...) do { if (p__debug_level >= 3) { fprintf(stdout, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); } } while(0)
#define DLOG2(_format, ...) do { if (p__debug_level >= 2) { fprintf(stdout, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); } } while(0)
#define DLOG(_format, ...) do { if (p__debug_level >= 1) { fprintf(stdout, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); } } while(0)
#else
#define DLOG3(_format, ...)
#define DLOG2(_format, ...)
#define DLOG(_format, ...)
#endif
#define ILOG(_format, ...) do { fprintf(stdout, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); } while(0)
#define WLOG(_format, ...) do { fprintf(stdout, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); } while(0)
#define ELOG(_format, ...) do { fprintf(stderr, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); } while(0)
#define FELOG(_format, ...) do { fprintf(stderr, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); } while(0)
#define ABORT(_format, ...) do { fprintf(stderr, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); abort(); } while(0)

#define _to_string(_x) std::to_string(_x)
#define STOI(_x) std::stoi(_x)

#endif

// Another weird android issue...
#if defined(ANDROID) && !defined(UINT32_MAX)
#define UINT32_MAX (4294967295U)
#endif

/// Things needed for implementing benchmarking standard
struct result_t
{
	uint64_t start;
	uint64_t end;
	int scene;
};
struct benchmarking
{
	std::vector<result_t> results; // store all results
	uint64_t init_time = 0; // to track start of whole run
	uint64_t latest_time = 0; // if we need it, to track start of latest iteration
	char* enable_file = nullptr; // copy of the enable file for the results file
	std::string test_name;
	std::string results_file; // path to results file
	std::vector<std::string> scene_name;
	std::vector<std::string> scene_result_file;
	std::string backend_name;
};

void bench_save_results_file(const benchmarking& b);
static inline void bench_init(benchmarking& b, const char* test_name, char* enable_file, const char* results_file)
{
	b.test_name = test_name;
	b.init_time = gettime();
	b.enable_file = enable_file;
	b.results_file = results_file;
}
static inline void bench_done(benchmarking& b)
{
	if (b.enable_file) { bench_save_results_file(b); free(b.enable_file); }
}
static inline void bench_start_iteration(benchmarking& b) { b.latest_time = gettime(); }
static inline void bench_stop_iteration(benchmarking& b) { b.results.push_back({ b.latest_time, gettime(), std::max<int>(0, (int)b.scene_name.size() - 1) }); }
static inline void bench_start_scene(benchmarking& b, const std::string& scene_name) { b.scene_name.push_back(scene_name); }
static inline void bench_stop_scene(benchmarking& b, const std::string& filename = std::string()) { b.scene_result_file.push_back(filename); }

static inline bool is_debug() { return p__debug_level; }
char keypress();
bool match(const char* in, const char* short_form, const char* long_form);
int get_arg(char** in, int i, int argc);
const char* get_string_arg(char** in, int i, int argc);
void usage();
char* load_blob(const std::string& filename, uint32_t* size);
void save_blob(const std::string& filename, const char* data, uint32_t size);
bool exists_blob(const std::string& filename);

int get_env_int(const char* name, int fallback);

static __attribute__((const)) inline uint64_t aligned_size(uint64_t size, uint64_t alignment) { return size + alignment - 1ull - (size + alignment - 1ull) % alignment; }
