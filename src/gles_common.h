#pragma once

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>

#ifdef SDL
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#elif X11
#define USE_X11
#endif

#include "util.h"

#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif

// Cross-platform compatibility macros. The shader macros are convenient
// for writing inline shaders, and also allow us to redefine them if we
// want to compile on native desktop GL one day.
#define GLSL_VS(src) { "#version 320 es\n" #src }
#define GLSL_FS(src) { "#version 320 es\nprecision mediump float;\n" #src }
#define GLSL_FS_5(src) { "#version 320 es\n#extension GL_EXT_gpu_shader5 : require\nprecision mediump float;\n" #src }
#define GLSL_CS(src) { "#version 320 es\n" #src }
#define GLSL_CONTROL(src) { "#version 320 es\n" #src }
#define GLSL_EVALUATE(src) { "#version 320 es\n" #src }
#define GLSL_GS(src) { "#version 320 es\n#extension GL_EXT_geometry_shader : enable\n" #src }

#define PACKED(definition) definition __attribute__((__packed__))

// our fake GL call
typedef void (GLAPIENTRY *PA_PFNGLASSERTBUFFERARMPROC)(GLenum target, GLsizei offset, GLsizei size, const char *md5);
extern PA_PFNGLASSERTBUFFERARMPROC glAssertBuffer_ARM;

struct TOOLSTEST;
typedef int (GLAPIENTRY *TOOLSTEST_CALLBACK_INIT)(TOOLSTEST *handle);
typedef void (GLAPIENTRY *TOOLSTEST_CALLBACK_SWAP)(TOOLSTEST *handle);
typedef void (GLAPIENTRY *TOOLSTEST_CALLBACK_FREE)(TOOLSTEST *handle);

typedef void (*TOOLSTEST_CALLBACK_USAGE)();
typedef bool (*TOOLSTEST_CALLBACK_CMDOPT)(int& i, int argc, char **argv);

struct TOOLSTEST
{
	std::string name;
	TOOLSTEST_CALLBACK_SWAP swap = nullptr;
	TOOLSTEST_CALLBACK_INIT init = nullptr;
	TOOLSTEST_CALLBACK_FREE done = nullptr;
	int times = 10;
	EGLint width = 640;
	EGLint height = 480;
	void *user_data = nullptr;
	EGLDisplay display = 0;
#ifdef SDL
	std::vector<SDL_GLContext> context;
	std::vector<SDL_Window*> surface;
#else
	std::vector<EGLContext> context;
	std::vector<EGLSurface> surface;
#endif
	int current_frame = 0;
	int debug = 0;

	benchmarking bench;
};

struct TOOLSTEST_INIT
{
	std::string name;
	TOOLSTEST_CALLBACK_SWAP swap = nullptr;
	TOOLSTEST_CALLBACK_INIT init = nullptr;
	TOOLSTEST_CALLBACK_FREE done = nullptr;
	TOOLSTEST_CALLBACK_USAGE usage = nullptr;
	TOOLSTEST_CALLBACK_CMDOPT cmdopt = nullptr;
	void *user_data = nullptr;
	EGLint *attribs = nullptr;
	int surfaces = 1;
};

int init(int argc, char** argv, const TOOLSTEST_INIT& init);
int init(int argc, char** argv, const char *name, TOOLSTEST_CALLBACK_SWAP swap, TOOLSTEST_CALLBACK_INIT setup, TOOLSTEST_CALLBACK_FREE cleanup, void *user_data = nullptr, EGLint *attribs = nullptr, int surfaces = 1);
void assert_fb(TOOLSTEST* handle);
void checkError(const char *msg);
void compile(const char *name, GLint shader);
void link_shader(const char *name, GLint program);
GLenum fb_internalformat();
bool is_null_run();
void annotate(const char *annotation);

void test_swap(TOOLSTEST* handle, int i = 0);
void test_makecurrent(TOOLSTEST* handle, int i = 0);
