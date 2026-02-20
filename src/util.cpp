#include "util.h"

#include "external/json.hpp"
#include <fstream>
#include <termios.h>
#include <unistd.h>
#include <string.h>

#if defined(_GNU_SOURCE) || defined(__BIONIC__)
#include <pthread.h>
#else
#include <sys/prctl.h>
#endif

#ifdef SDL
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#endif

int get_env_int(const char* name, int fallback)
{
	int v = fallback;
	const char* tmpstr = getenv(name);
	if (tmpstr)
	{
		v = atoi(tmpstr);
	}
	return v;
}

uint_fast32_t p__loops = get_env_int("TOOLSTEST_TIMES", 10);
uint_fast8_t p__sanity = get_env_int("TOOLSTEST_SANITY", 0);
uint_fast8_t p__debug_level = get_env_int("TOOLSTEST_DEBUG", 0);
uint_fast8_t p__validation = get_env_int("TOOLSTEST_VALIDATION", 0);
int_fast8_t p__device = get_env_int("TOOLSTEST_DEVICE", -1);

void bench_save_results_file(const benchmarking& b)
{
	printf("Writing benchmarking results file (%d iterations): %s\n", (int)b.results.size(), b.results_file.c_str());
	nlohmann::json data;
	data["app_version"] = "1.0";
	data["std_version"] = 1;
	data["enable_file"] = nlohmann::json::parse(b.enable_file);
	if (!b.backend_name.empty()) data["rendering_backend"] = b.backend_name;
	data["init_time"] = b.init_time;
	data["end_time"] = gettime();
	nlohmann::json results = nlohmann::json::array();
	for (const auto& v : b.results)
	{
		nlohmann::json result;
		if (!b.scene_name.empty())
		{
			result["scene"] = b.scene_name.at(v.scene);
			if ((int)b.scene_result_file.size() >= v.scene && !b.scene_result_file.at(v.scene).empty())
			{
				result["output"] = b.scene_result_file.at(v.scene);
				result["putput_type"] = "png";
				result["validated"] = false;
			}
		}
		result["start_time"] = v.start;
		result["stop_time"] = v.end;
		result["time"] = v.end - v.start;
		results.push_back(result);
	}
	data["results"] = results;
	std::ofstream file(b.results_file);
	file << data.dump(4);
	file.close();
}

void set_thread_name(const char* name)
{
	// "length is restricted to 16 characters, including the terminating null byte"
	// http://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
	assert(strlen(name) <= 15);
#if defined(_GNU_SOURCE) || defined(__BIONIC__)
	pthread_setname_np(pthread_self(), name);
#else
	prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
#endif
}

char keypress()
{
	char ch = 0;
#if SDL
	SDL_Event event;
	while (true)
	{
		bool any = SDL_PollEvent(&event);
		if (any && event.type == SDL_KEYDOWN && event.key.keysym.scancode < 0x80 && event.key.keysym.scancode > 0) return (char)event.key.keysym.scancode;
		else if (any && event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) return 'q';
	}
#else
	struct termios argin;
	struct termios argout;
	tcgetattr(0, &argin);
	argout = argin;
	argout.c_lflag &= ~(ICANON);
	argout.c_iflag &= ~(ICRNL);
	argout.c_oflag &= ~(OPOST);
	argout.c_cc[VMIN] = 1;
	argout.c_cc[VTIME] = 0;
	tcsetattr(0, TCSADRAIN, &argout);
	size_t r = read(0, &ch, 1);
	(void)r; // make compiler happy
	tcsetattr(0, TCSADRAIN, &argin);
#endif
	return ch;
}

bool match(const char* in, const char* short_form, const char* long_form)
{
	if ((short_form && strcmp(in, short_form) == 0) || (long_form && strcmp(in, long_form) == 0))
	{
		return true;
	}
	return false;
}

int get_arg(char** in, int i, int argc)
{
	if (i == argc)
	{
		ELOG("Missing command line parameter\n");
		exit(-1);
	}
	return atoi(in[i]);
}

const char* get_string_arg(char** in, int i, int argc)
{
	if (i == argc)
	{
		ELOG("Missing command line parameter\n");
		exit(-1);
	}
	return in[i];
}

bool exists_blob(const std::string& filename)
{
	FILE* fp = fopen(filename.c_str(), "rb");
	if (!fp) return false;
	struct stat st = {};
	int r = fstat(fileno(fp), &st);
	fclose(fp);
	return (r == 0 && st.st_size > 0);
}

char* load_blob(const std::string& filename, uint32_t* size)
{
	FILE* fp = fopen(filename.c_str(), "rb");
	if (!fp)
	{
		ABORT("Cannot open \"%s\": %s", filename.c_str(), strerror(errno));
	}
	struct stat st;
	int r = fstat(fileno(fp), &st);
	if (r != 0) ABORT("Could not stat \"%s\": %s", filename.c_str(), strerror(errno));
	if (st.st_size == 0) ABORT("Trying to load blob of size zero!");
	char* blob = (char*)malloc(st.st_size);
	r = fread(blob, st.st_size, 1, fp);
	if (r != 1) ABORT("Could not read \"%s\" (size %d, returned %d): %s", filename.c_str(), (int)st.st_size, r, strerror(errno));
	fclose(fp);
	*size = st.st_size;
	return blob;
}

void save_blob(const std::string& filename, const char* data, uint32_t size)
{
	if (size == 0) ABORT("Trying to save blob of size zero!");
	FILE* fp = fopen(filename.c_str(), "wb");
	if (!fp)
	{
		ABORT("Cannot open \"%s\": %s", filename.c_str(), strerror(errno));
	}
	int r = fwrite(data, size, 1, fp);
	if (r != 1) ABORT("Could not write \"%s\" (size %d, returned %d): %s", filename.c_str(), (int)size, r, strerror(errno));
	fclose(fp);
}
