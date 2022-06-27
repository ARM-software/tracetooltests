#include "gles_common.h"
#include <EGL/eglext.h>

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

static int get_env_int(const char* name, int fallback)
{
	int v = fallback;
	const char* tmpstr = getenv(name);
	if (tmpstr)
	{
		v = atoi(tmpstr);
	}
	return v;
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

static void usage(TOOLSTEST_CALLBACK_USAGE usage)
{
	printf("Usage:\n");
	printf("-h/--help              This help\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default 0)\n");
	printf("-t/--times N           Times to repeat (default 10)\n");
	printf("-s/--step              Step mode\n");
	printf("-i/--inject            Inject sanity checking\n");
	printf("-n/--null-run          Skip testing of results\n");
	if (usage) usage();
	exit(1);
}

int init(int argc, char** argv, const TOOLSTEST_INIT& init)
{
	const EGLint *attribs = init.attribs;
	TOOLSTEST handle;
	handle.name = init.name;
	handle.swap = init.swap;
	handle.init = init.init;
	handle.done = init.done;
	handle.times = get_env_int("TOOLSTEST_TIMES", 10);
	handle.user_data = init.user_data;
	handle.current_frame = 0;

	if (get_env_int("TOOLSTEST_STEP", 0) > 0) step_mode = true;
	inject_asserts = (bool)get_env_int("TOOLSTEST_SANITY", 0);
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
		else
		{
			if (!init.cmdopt || !init.cmdopt(i, argc, argv))
			{
				ELOG("Unrecognized cmd line parameter: %s", argv[i]);
				usage(init.usage);
			}
		}
	}

#ifdef SDL
	SDL_SetMainReady();
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		ELOG("SDL could not initialize! SDL_Error: %s", SDL_GetError());
		return -2;
	}
	atexit(SDL_Quit);
#else

#if X11
	Display* display = XOpenDisplay(nullptr);
	handle.display = eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, display, nullptr);
#else
	handle.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif

	PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (eglQueryDevicesEXT && eglGetPlatformDisplayEXT && handle.display == EGL_NO_DISPLAY)
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

	const EGLint surfaceAttribs[] = {
		EGL_SURFACE_TYPE,
#if PBUFFERS
		EGL_PBUFFER_BIT,
#else
		EGL_WINDOW_BIT,
#endif
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_ALPHA_SIZE, EGL_DONT_CARE,
		EGL_STENCIL_SIZE, EGL_DONT_CARE,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_NONE, EGL_NONE,
	};
	const EGLint contextAttribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 2,
		EGL_NONE, EGL_NONE,
	};
	if (attribs == nullptr) attribs = surfaceAttribs; // use defaults

	EGLint majorVersion;
	EGLint minorVersion;
	if (!eglInitialize(handle.display, &majorVersion, &minorVersion))
	{
		ELOG("eglInitialize() failed");
		return -6;
	}
	DLOG("EGL version is %d.%d", majorVersion, minorVersion);

	EGLint numConfigs = 0;
	if (!eglChooseConfig(handle.display, attribs, nullptr, 0, &numConfigs))
	{
		ELOG("eglChooseConfig(null) failed");
		eglTerminate(handle.display);
		return -7;
	}
	std::vector<EGLConfig> configs(numConfigs);
	if (!eglChooseConfig(handle.display, attribs, configs.data(), configs.size(), &numConfigs))
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
		handle.context[j] = eglCreateContext(handle.display, configs[0], (j == 0) ? EGL_NO_CONTEXT : handle.context[0], contextAttribs);
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
		init.swap(&handle);
		test_swap(&handle);
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

int init(int argc, char** argv, const char *name, TOOLSTEST_CALLBACK_SWAP swap, TOOLSTEST_CALLBACK_INIT setup, TOOLSTEST_CALLBACK_FREE cleanup, void *user_data, EGLint *attribs, int surfaces)
{
	TOOLSTEST_INIT initparam;
	initparam.name = name;
	initparam.swap = swap;
	initparam.init = setup;
	initparam.done = cleanup;
	initparam.user_data = user_data,
	initparam.attribs = attribs;
	initparam.surfaces = surfaces;
	return init(argc, argv, initparam);
}

// before calling this, add appropriate memory barriers
void assert_fb(TOOLSTEST* handle)
{
	if (!inject_asserts) return;

	GLuint pbo;
	GLenum internalformat = fb_internalformat();
	int mult;
	GLenum format;
	GLenum type;

	switch (internalformat)
	{
	case GL_RGB565: mult = 2; format = GL_RGB; type = GL_UNSIGNED_SHORT_5_6_5; break;
	case GL_RGB8: mult = 3; format = GL_RGB; type = GL_UNSIGNED_BYTE; break;
	case GL_RGBA8: mult = 4; format = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
	default: ELOG("Bad internal format"); abort(); break;
	}
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
