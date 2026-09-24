#include "gles_common.h"
#include <EGL/eglext.h>

#include "external/json.hpp"
#include <climits>
#include <fstream>
#include <vector>

#define IT_WIDTH 1024
#define IT_HEIGHT 640

#ifdef FBDEV
struct fbdev_window
{
	unsigned short width;
	unsigned short height;
};
static std::vector<fbdev_window> windows;
#elif X11
static std::vector<Window> windows;
#endif

static bool null_run = false;
static bool inject_asserts = false;
static PFNGLINSERTEVENTMARKEREXTPROC my_glInsertEventMarkerEXT = nullptr;
static bool step_mode = false;

static void dummy_glAssertBuffer_ARM(GLenum target, GLsizei offset, GLsizei size, const char *md5)
{
	(void)target;
	(void)offset;
	(void)size;
	(void)md5;
	// nothing happens here
}

PA_PFNGLASSERTBUFFERARMPROC glAssertBuffer_ARM = dummy_glAssertBuffer_ARM;

static void debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
	if (type == GL_DEBUG_TYPE_PUSH_GROUP_KHR || type == GL_DEBUG_TYPE_POP_GROUP_KHR)
	{
		return; // don't care
	}
	ELOG("OpenGL message: %s", message);
	if (type == GL_DEBUG_TYPE_ERROR_KHR)
	{
		abort();
	}
}

static bool check_bench(TOOLSTEST& b, const TOOLSTEST_INIT& reqs)
{
	const char* enable_json = getenv("BENCHMARKING_ENABLE_JSON");
	const char* enable_path = getenv("BENCHMARKING_ENABLE_PATH");
	const std::string our_name = "gles_" + b.name;
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
	if (data.value("target", "no target") != our_name) { printf("Name in benchmarking enable file is not ours - skipping\n"); return false; }

	if (data.count("capabilities"))
	{
		nlohmann::json caps = data.at("capabilities");

		p__loops = caps.value("loops", p__loops);
	}

	bench_init(b.bench, our_name.c_str(), content, data.value("results", "results.json").c_str());

	return true;
}

GLenum fb_internalformat()
{
	GLint red = 0;
	GLint alpha = 0;

	glGetIntegerv(GL_RED_BITS, &red);
	glGetIntegerv(GL_ALPHA_BITS, &alpha);
	if (red == 5)
	{
		DLOG("GL_RGB565");
		return GL_RGB565;
	}
	else if (alpha == 0)
	{
		DLOG("GL_RGB8");
		return GL_RGB8;
	}
	else
	{
		DLOG("GL_RGBA8");
		return GL_RGBA8;
	}
}

bool is_null_run()
{
	return null_run;
}

void annotate(const char *annotation)
{
	if (my_glInsertEventMarkerEXT) my_glInsertEventMarkerEXT(0, annotation);
}

static bool has_extension(const char* name)
{
	GLint max = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &max);
	for (int i = 0; i < max; i++)
	{
		const char* s = (const char*)glGetStringi(GL_EXTENSIONS, i);
		if (strcmp(s, name) == 0) return true;
	}
	return false;
}

#if defined(X11) || defined(PBUFFERS)
static bool has_egl_extension(const char* extensions, const char* name)
{
	if (!extensions) return false;
	const char* start = extensions;
	const size_t length = strlen(name);
	while ((extensions = strstr(extensions, name)))
	{
		if ((extensions == start || extensions[-1] == ' ') && (extensions[length] == ' ' || extensions[length] == '\0')) return true;
		extensions += length;
	}
	return false;
}
#endif

static void usage(TOOLSTEST_CALLBACK_USAGE usage)
{
	printf("Usage:\n");
	printf("-h/--help              This help\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default 0)\n");
	printf("-t/--times N           Times to repeat (default 10)\n");
	printf("-V/--variant M N       GLES major and minor versions for context initialization (default 3.2)\n");
	printf("-G/--gpu               Select a hardware EGL device (X11 or pbuffers)\n");
	printf("-C/--cpu               Select a software EGL device (X11 or pbuffers)\n");
	printf("-D/--device N          Select EGL device by index (X11 or pbuffers)\n");
	printf("-s/--step              Step mode\n");
	printf("-i/--inject            Inject sanity checking\n");
	printf("-n/--null-run          Skip testing of results\n");
	if (usage) usage();
	exit(1);
}

int init(int argc, char** argv, const TOOLSTEST_INIT& init)
{
	TOOLSTEST handle;
	handle.name = init.name;
	handle.swap = init.swap;
	handle.init = init.init;
	handle.done = init.done;
	handle.times = p__loops;
	handle.user_data = init.user_data;
	handle.current_frame = 0;
	int major_version = init.major_version;
	int minor_version = init.minor_version;
	int device_index = -1;
	bool select_gpu = false;
	bool select_cpu = false;
#if defined(X11) || defined(PBUFFERS)
	EGLDeviceEXT selected_device = EGL_NO_DEVICE_EXT;
#endif

	if (get_env_int("TOOLSTEST_STEP", 0) > 0) step_mode = true;
	inject_asserts = (bool)p__sanity;
	null_run = (bool)get_env_int("TOOLSTEST_NULL_RUN", 0);
	if (null_run)
	{
		DLOG("Doing a null run - not checking results!");
	}

	for (int i = 1; i < argc; i++)
	{
		if (match(argv[i], "-h", "--help"))
		{
			usage(init.usage);
		}
		else if (match(argv[i], "-d", "--debug"))
		{
			handle.debug = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-V", "--variant"))
		{
			major_version = get_arg(argv, ++i, argc);
			minor_version = get_arg(argv, ++i, argc);
			if (major_version > 3 || minor_version > 2 || major_version < 0 || minor_version < 0)
			{
				printf("Bad GLES version: %d.%d\n", major_version, minor_version);
				exit(-1);
			}
		}
		else if (match(argv[i], "-n", "--null-run"))
		{
			null_run = true;
		}
		else if (match(argv[i], "-i", "--inject"))
		{
			inject_asserts = true;
		}
		else if (match(argv[i], "-s", "--step"))
		{
			step_mode = true;
		}
		else if (match(argv[i], "-t", "--times"))
		{
			handle.times = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-D", "--device"))
		{
			const char* argument = get_string_arg(argv, ++i, argc);
			char* end = nullptr;
			const long index = strtol(argument, &end, 10);
			if (end == argument || *end != '\0' || index < 0 || index > INT_MAX)
			{
				ELOG("Invalid EGL device index: %s", argument);
				return -1;
			}
			device_index = index;
		}
		else if (match(argv[i], "-G", "--gpu"))
		{
			select_gpu = true;
		}
		else if (match(argv[i], "-C", "--cpu"))
		{
			select_cpu = true;
		}
		else
		{
			if (!init.cmdopt || !init.cmdopt(i, argc, argv))
			{
				ELOG("Unrecognized cmd line parameter: %s", argv[i]);
				usage(init.usage);
			}
		}
	}
	if (select_gpu && select_cpu)
	{
		ELOG("You cannot combine --gpu and --cpu");
		return -1;
	}

	handle.bench.backend_name = "GLES " + std::to_string(major_version) + "." + std::to_string(minor_version);
	check_bench(handle, init);

#ifdef X11
	Display* display = nullptr;
#endif

	if (device_index != -1 || select_gpu || select_cpu)
	{
#if defined(SDL) || defined(FBDEV)
		ELOG("GLES device selection is not supported with this window system");
		return 77;
#else
		const char* client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
		if (!has_egl_extension(client_extensions, "EGL_EXT_device_enumeration"))
		{
			ELOG("EGL device enumeration is not supported");
			return 77;
		}
#ifdef X11
		if (!has_egl_extension(client_extensions, "EGL_EXT_explicit_device"))
		{
			ELOG("EGL_EXT_explicit_device is required for X11 device selection");
			return 77;
		}
#else
		if (!has_egl_extension(client_extensions, "EGL_EXT_platform_device"))
		{
			ELOG("EGL_EXT_platform_device is required for pbuffer device selection");
			return 77;
		}
#endif
		PFNEGLQUERYDEVICESEXTPROC query_devices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
		if (!query_devices)
		{
			ELOG("eglQueryDevicesEXT is unavailable");
			return 77;
		}
		EGLint num_devices = 0;
		if (!query_devices(0, nullptr, &num_devices))
		{
			ELOG("Failed to poll EGL devices: 0x%04x", (unsigned)eglGetError());
			return -3;
		}
		if (num_devices == 0)
		{
			ELOG("No EGL devices found");
			return 77;
		}
		std::vector<EGLDeviceEXT> devices(num_devices);
		if (!query_devices(num_devices, devices.data(), &num_devices))
		{
			ELOG("Failed to fetch EGL devices: 0x%04x", (unsigned)eglGetError());
			return -4;
		}
		assert(num_devices <= (EGLint)devices.size());
		PFNEGLQUERYDEVICESTRINGEXTPROC query_device_string = (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
#ifdef EGL_EXT_device_type
		PFNEGLQUERYDEVICEATTRIBEXTPROC query_device_attrib = (PFNEGLQUERYDEVICEATTRIBEXTPROC)eglGetProcAddress("eglQueryDeviceAttribEXT");
#endif
		bool mesa_software_available = false;
		int matching_device = -1;
		printf("Found %d EGL devices\n", (int)num_devices);
		for (EGLint device = 0; device < num_devices; device++)
		{
			const char* extensions = query_device_string ? query_device_string(devices[device], EGL_EXTENSIONS) : nullptr;
			const char* renderer = has_egl_extension(extensions, "EGL_EXT_device_query_name") ? query_device_string(devices[device], EGL_RENDERER_EXT) : nullptr;
			const bool mesa_software = has_egl_extension(extensions, "EGL_MESA_device_software");
			if (mesa_software) mesa_software_available = true;
			bool is_cpu = mesa_software;
			bool is_gpu = false;
#ifdef EGL_EXT_device_type
			EGLAttrib device_type = 0;
			if (has_egl_extension(extensions, "EGL_EXT_device_type") && query_device_attrib &&
			    query_device_attrib(devices[device], EGL_DEVICE_TYPE_EXT, &device_type))
			{
				is_cpu = device_type == EGL_DEVICE_TYPE_CPU_EXT;
				is_gpu = device_type == EGL_DEVICE_TYPE_INTEGRATED_GPU_EXT || device_type == EGL_DEVICE_TYPE_DISCRETE_GPU_EXT;
			}
#endif
			if (!is_cpu && !is_gpu && query_device_string)
			{
				const char* render_node = has_egl_extension(extensions, "EGL_EXT_device_drm_render_node") ?
				                          query_device_string(devices[device], EGL_DRM_RENDER_NODE_FILE_EXT) : nullptr;
				is_gpu = render_node && render_node[0];
			}
			printf("\t%d : %s (%s)\n", (int)device, renderer ? renderer : "<name unavailable>", is_cpu ? "CPU" : is_gpu ? "GPU" : "unknown");
			if (((select_cpu && is_cpu) || (select_gpu && is_gpu)) && matching_device == -1 &&
			    (device_index == -1 || device_index == device)) matching_device = device;
		}
		if (device_index >= num_devices)
		{
			ELOG("EGL device %d does not exist", device_index);
			return -1;
		}
		if (select_gpu || select_cpu)
		{
			if (matching_device == -1)
			{
				ELOG("No EGL %s device found%s", select_cpu ? "CPU" : "GPU", device_index == -1 ? "" : " at the requested index");
				return 77;
			}
			device_index = matching_device;
			if (mesa_software_available && setenv("LIBGL_ALWAYS_SOFTWARE", select_cpu ? "1" : "0", 1) != 0)
			{
				ELOG("Unable to configure Mesa device selection");
				return -1;
			}
		}
		selected_device = devices[device_index];
#ifdef X11
		display = XOpenDisplay(nullptr);
		if (!display)
		{
			ELOG("Unable to open X display");
			return -5;
		}
		const EGLAttrib attributes[] = { EGL_DEVICE_EXT, reinterpret_cast<EGLAttrib>(selected_device), EGL_NONE };
		handle.display = eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, display, attributes);
#else
		handle.display = eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, selected_device, nullptr);
#endif
		if (handle.display == EGL_NO_DISPLAY)
		{
			ELOG("Unable to create display for EGL device %d: 0x%04x", device_index, (unsigned)eglGetError());
#ifdef X11
			XCloseDisplay(display);
#endif
			return 77;
		}
#endif
	}

#ifdef SDL
	SDL_SetMainReady();
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		ELOG("SDL could not initialize! SDL_Error: %s", SDL_GetError());
		return -2;
	}
	atexit(SDL_Quit);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
	                    SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major_version);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor_version);
#else

#if X11
	if (device_index == -1) display = XOpenDisplay(nullptr);
	if (device_index == -1) handle.display = eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, display, nullptr);
#else
	if (device_index == -1) handle.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif

	PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (device_index == -1 && eglQueryDevicesEXT && eglGetPlatformDisplayEXT && handle.display == EGL_NO_DISPLAY)
	{
		EGLint numDevices = 0;
		if (eglQueryDevicesEXT(0, nullptr, &numDevices) == EGL_FALSE)
		{
			ELOG("Failed to poll EGL devices");
			return -3;
		}
		std::vector<EGLDeviceEXT> devices(numDevices);
		if (eglQueryDevicesEXT(devices.size(), devices.data(), &numDevices) == EGL_FALSE)
		{
			ELOG("Failed to fetch EGL devices");
			return -4;
		}
		for (unsigned i = 0; i < devices.size(); i++)
		{
			handle.display = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, devices[i], 0);
			DLOG("Using EGL device %u for our display!", i);
			break;
		}
	}

	if (handle.display == EGL_NO_DISPLAY)
	{
		ELOG("No display found");
		return -5;
	}

	EGLint renderable_type = (major_version <= 1) ? EGL_OPENGL_ES_BIT : EGL_OPENGL_ES2_BIT;
	const EGLint surfaceAttribs[] = {
		EGL_SURFACE_TYPE,
#if PBUFFERS
		EGL_PBUFFER_BIT,
#else
		EGL_WINDOW_BIT,
#endif
		EGL_RENDERABLE_TYPE, renderable_type,
		EGL_ALPHA_SIZE, EGL_DONT_CARE,
		EGL_STENCIL_SIZE, EGL_DONT_CARE,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_NONE, EGL_NONE,
	};
	const EGLint contextAttribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, major_version,
		EGL_CONTEXT_MINOR_VERSION, minor_version,
		EGL_NONE, EGL_NONE,
	};
	const EGLint* surface_attribs = surfaceAttribs;
	const EGLint* context_attribs = contextAttribs;
	if (init.surface_attribs) surface_attribs = init.surface_attribs; // override
	if (init.context_attribs) context_attribs = init.context_attribs; // override

	EGLint majorVersion;
	EGLint minorVersion;
	if (!eglInitialize(handle.display, &majorVersion, &minorVersion))
	{
		ELOG("eglInitialize() failed");
		return -6;
	}
	DLOG("EGL version is %d.%d", majorVersion, minorVersion);
#if defined(X11) || defined(PBUFFERS)
	if (device_index != -1 && has_egl_extension(eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS), "EGL_EXT_device_query"))
	{
		PFNEGLQUERYDISPLAYATTRIBEXTPROC query_display_attrib = (PFNEGLQUERYDISPLAYATTRIBEXTPROC)eglGetProcAddress("eglQueryDisplayAttribEXT");
		EGLAttrib active_device = 0;
		if (!query_display_attrib || !query_display_attrib(handle.display, EGL_DEVICE_EXT, &active_device) ||
		    active_device != reinterpret_cast<EGLAttrib>(selected_device))
		{
			ELOG("EGL display did not select device %d", device_index);
			eglTerminate(handle.display);
#ifdef X11
			XCloseDisplay(display);
#endif
			return 77;
		}
	}
	if (device_index != -1) printf("Selecting EGL device %d\n", device_index);
#endif

	EGLint numConfigs = 0;
	if (!eglChooseConfig(handle.display, surface_attribs, nullptr, 0, &numConfigs))
	{
		ELOG("eglChooseConfig(null) failed");
		eglTerminate(handle.display);
		return -7;
	}
	std::vector<EGLConfig> configs(numConfigs);
	if (!eglChooseConfig(handle.display, surface_attribs, configs.data(), configs.size(), &numConfigs))
	{
		ELOG("eglChooseConfig() failed");
		eglTerminate(handle.display);
		return -8;
	}

	EGLint selected = -1;
	for (EGLint i = 0; i < (EGLint)configs.size(); i++)
	{
		EGLConfig config = configs[i];
		EGLint value = -1;
		if (!eglGetConfigAttrib(handle.display, config, EGL_RED_SIZE, &value)) abort();
		if (value == 8) // make sure we avoid the 10bit formats
		{
			selected = i;
			break;
		}
	}
	DLOG("Found %d EGL configs, selected %d", (int)configs.size(), selected);
	assert(selected != -1);
#endif

	handle.surface.resize(init.surfaces);
	handle.context.resize(init.surfaces);
#if defined(X11) || defined(FBDEV)
	windows.resize(init.surfaces);
#endif
	for (int j = 0; j < init.surfaces; j++)
	{
		std::string wname = std::string(init.name) + "_w" + std::to_string(j);
#ifdef FBDEV
		windows[j] = { IT_HEIGHT, IT_WIDTH };
		handle.surface[j] = eglCreateWindowSurface(handle.display, configs[selected], (intptr_t)(&windows[j]), nullptr);
#elif X11
		Window root = RootWindow(display, DefaultScreen(display));

		EGLint nativeVisualId = 0;
		eglGetConfigAttrib(handle.display, configs[selected], EGL_NATIVE_VISUAL_ID, &nativeVisualId);
		XVisualInfo tempVI;
		tempVI.visualid = nativeVisualId;
		int visualCnt = 0;
		XVisualInfo* visualInfo = XGetVisualInfo(display, VisualIDMask, &tempVI, &visualCnt);
		if (!visualInfo) abort();

		XSetWindowAttributes attr;
		attr.background_pixel = 0;
		attr.border_pixel = 0;
		attr.colormap = XCreateColormap(display, root, visualInfo->visual, AllocNone);
		if (attr.colormap == None) abort();
		attr.event_mask = StructureNotifyMask;

		unsigned long mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
		int x = 0;
		int y = 0;
		windows[j] = XCreateWindow(display, root, x, y, IT_WIDTH, IT_HEIGHT, 0, visualInfo->depth, InputOutput, visualInfo->visual, mask, &attr);

		XSizeHints sizehints;
		sizehints.x = 0;
		sizehints.y = 0;
		sizehints.width  = IT_WIDTH;
		sizehints.height = IT_HEIGHT;
		sizehints.flags = USSize | USPosition;
		XSetNormalHints(display, windows[j], &sizehints);
		XSelectInput(display, windows[j], StructureNotifyMask | KeyPressMask | ButtonPressMask);

		XSetStandardProperties(display, windows[j], wname.c_str(), wname.c_str(), None, (char **)NULL, 0, &sizehints);

		eglWaitNative(EGL_CORE_NATIVE_ENGINE);
		handle.surface[j] = eglCreateWindowSurface(handle.display, configs[selected], windows[j], nullptr);
		XMapWindow(display, windows[j]);
		XFree(visualInfo);
		XFreeColormap(display, attr.colormap);
#elif SDL
		handle.surface[j] = SDL_CreateWindow(wname.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, IT_WIDTH, IT_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
		if (handle.surface[j] == EGL_NO_SURFACE)
		{
			ELOG("Failed to create SDL window: %s", SDL_GetError());
			return -9;
		}
		handle.context[j] = SDL_GL_CreateContext(handle.surface[j]);
		ILOG("Created context %lu with driver %s on %s\n", (unsigned long)handle.context[j], SDL_GetCurrentVideoDriver(), SDL_GetDisplayName(SDL_GetWindowDisplayIndex(handle.surface[j])));
#elif PBUFFERS
		EGLint pAttribs[] = {
			EGL_HEIGHT, (EGLint)IT_HEIGHT,
			EGL_WIDTH, (EGLint)IT_WIDTH,
			EGL_NONE, EGL_NONE,
		};
		handle.surface[j] = eglCreatePbufferSurface(handle.display, configs[selected], pAttribs);
#endif

#if defined(FBDEV) || defined(PBUFFERS) || defined(X11)
		if (handle.surface[j] == EGL_NO_SURFACE)
		{
			ELOG("create surface failed: 0x%04x", (unsigned)eglGetError());
			return -10;
		}
		handle.context[j] = eglCreateContext(handle.display, configs[selected], (j == 0) ? EGL_NO_CONTEXT : handle.context[0], context_attribs);
		if (handle.context[j] == EGL_NO_CONTEXT)
		{
			ELOG("eglCreateContext() failed: 0x%04x", (unsigned)eglGetError());
			return -11;
		}
#endif
	}

#if defined(FBDEV) || defined(PBUFFERS) || defined(X11)
	if (!eglMakeCurrent(handle.display, handle.surface[0], handle.surface[0], handle.context[0]))
 	{
		ELOG("eglMakeCurrent() failed");
		return -12;
	}
	EGLint egl_context_client_version;
	eglQueryContext(handle.display, handle.context[0], EGL_CONTEXT_CLIENT_VERSION, &egl_context_client_version);
	DLOG("EGL client version %d", egl_context_client_version);
	eglQuerySurface(handle.display, handle.surface[0], EGL_WIDTH, &handle.width);
	eglQuerySurface(handle.display, handle.surface[0], EGL_HEIGHT, &handle.height);
	DLOG("Surface resolution %p(%d, %d)", handle.surface[0], handle.width, handle.height);
#else
	if (SDL_GL_MakeCurrent(handle.surface[0], handle.context[0]) != 0)
	{
		ELOG("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
	}
#endif

	printf("GL renderer: %s\n", (const char*)glGetString(GL_RENDERER));
	my_glInsertEventMarkerEXT = (PFNGLINSERTEVENTMARKEREXTPROC)eglGetProcAddress("glInsertEventMarkerEXT");

	// if a tool implements this function, replace our dummy with its real implementation
	void* ptr = (void*)eglGetProcAddress("glAssertBuffer_ARM");
	if (has_extension("GL_ARM_buffer_validation") && ptr)
	{
		DLOG("We found an implementation of glAssertBuffer_ARM (%p)", ptr);
		glAssertBuffer_ARM = (PA_PFNGLASSERTBUFFERARMPROC)ptr;
	}
	glEnable(GL_DEBUG_OUTPUT_KHR); // use GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR for serious debugging
	glDebugMessageCallback(debug_callback, NULL);

	int ret = init.init(&handle);
	if (ret != 0)
	{
		ELOG("Setup failed");
		return ret;
	}
	for (int i = 0; i < handle.times; i++)
	{
		handle.current_frame = i;
		std::string annotation = std::string(init.name) + " frame " + std::to_string(handle.current_frame);
		annotate(annotation.c_str());
		bench_start_iteration(handle.bench);
		init.swap(&handle);
		test_swap(&handle);
		bench_stop_iteration(handle.bench);
		if (step_mode)
		{
			char c = keypress();
#if defined(FBDEV) || defined(PBUFFERS) || defined(X11)
			if (c == 'q') { eglTerminate(handle.display); return 0; }
#else
			if (c == 'q') { eglTerminate(handle.display); SDL_Quit(); return 0; }
#endif
		}
	}
	bench_done(handle.bench);
	init.done(&handle);

	eglMakeCurrent(handle.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

#if defined(FBDEV) || defined(PBUFFERS) || defined(X11)
	for (unsigned j = 0; j < handle.context.size(); j++) eglDestroyContext(handle.display, handle.context[j]);
	for (unsigned j = 0; j < handle.surface.size(); j++) eglDestroySurface(handle.display, handle.surface[j]);
#else
	for (unsigned j = 0; j < handle.context.size(); j++) SDL_GL_DeleteContext(handle.context[j]);
#endif
#ifdef X11
	eglWaitClient();
	for (unsigned j = 0; j < handle.surface.size(); j++) XUnmapWindow(display, windows[j]);
	eglWaitNative(EGL_CORE_NATIVE_ENGINE);
	for (unsigned j = 0; j < handle.surface.size(); j++) XDestroyWindow(display, windows[j]);
	XCloseDisplay(display);
#endif

	handle.context.clear();
	handle.surface.clear();
	eglTerminate(handle.display);
#ifdef SDL
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
#endif

	return 0;
}

int init(int argc, char** argv, const char *name, TOOLSTEST_CALLBACK_SWAP swap, TOOLSTEST_CALLBACK_INIT setup, TOOLSTEST_CALLBACK_FREE cleanup, void *user_data, EGLint *surface_attribs, int surfaces)
{
	TOOLSTEST_INIT initparam;
	initparam.name = name;
	initparam.swap = swap;
	initparam.init = setup;
	initparam.done = cleanup;
	initparam.user_data = user_data,
	initparam.surface_attribs = surface_attribs;
	initparam.surfaces = surfaces;
	return init(argc, argv, initparam);
}

// before calling this, add appropriate memory barriers
void assert_fb(TOOLSTEST* handle)
{
	if (!inject_asserts) return;

	GLuint pbo;
	const int mult = 4;
	const GLenum format = GL_RGBA;
	const GLenum type = GL_UNSIGNED_BYTE;
	glGenBuffers(1, &pbo);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
	glBufferData(GL_PIXEL_PACK_BUFFER, handle->width * handle->height * mult, NULL, GL_DYNAMIC_READ);
	glReadPixels(0, 0, handle->width, handle->height, format, type, 0);
	GLsync s = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	GLenum e = glClientWaitSync(s, GL_SYNC_FLUSH_COMMANDS_BIT, 100 * 1000 * 1000);
	if (e == GL_TIMEOUT_EXPIRED) // we get this on Note3, not sure why
	{
		DLOG("Wait for sync object timed out");
	}
	else if (e != GL_CONDITION_SATISFIED && e != GL_ALREADY_SIGNALED)
	{
		ELOG("Wait for sync object failed, got %x as response", e);
	}
	glDeleteSync(s);
	glAssertBuffer_ARM(GL_PIXEL_PACK_BUFFER, 0, handle->width * handle->height * mult, "0123456789abcdef");
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glDeleteBuffers(1, &pbo);
}

void compile(const char *name, GLint shader)
{
	GLint rvalue;
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &rvalue);
	if (!rvalue)
	{
		GLint maxLength = 0, len = -1;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
		char *infoLog = NULL;
		if (maxLength > 0)
		{
			infoLog = (char *)malloc(maxLength);
			glGetShaderInfoLog(shader, maxLength, &len, infoLog);
		}
		ELOG("Error in compiling %s (%d): %s", name, len, infoLog ? infoLog : "(n/a)");
		free(infoLog);
		abort();
	}
}

void link_shader(const char *name, GLint program)
{
	GLint rvalue;
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &rvalue);
	if (!rvalue)
	{
		GLint maxLength = 0, len = -1;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
		char *infoLog = (char *)NULL;
		if (maxLength > 0)
		{
			infoLog = (char *)malloc(maxLength);
			glGetProgramInfoLog(program, maxLength, &len, infoLog);
		}
		ELOG("Error in linking %s (%d): %s", name, len, infoLog ? infoLog : "(n/a)");
		free(infoLog);
		abort();
	}
}

void test_swap(TOOLSTEST* handle, int i)
{
#if defined(FBDEV) || defined(PBUFFERS) || defined(X11)
	eglSwapBuffers(handle->display, handle->surface[i]);
#else
	SDL_GL_SwapWindow(handle->surface[i]);
#endif
}

void test_makecurrent(TOOLSTEST* handle, int i)
{
#if defined(FBDEV) || defined(PBUFFERS) || defined(X11)
	eglMakeCurrent(handle->display, handle->surface[i], handle->surface[i], handle->context[i]);
#else
	SDL_GL_MakeCurrent(handle->surface[i], handle->context[i]);
#endif
}
