// Test the ANDROID_extension_pack_es31a extension

#include "gles_common.h"

static int setupGraphics(TOOLSTEST *handle)
{
	glViewport(0, 0, handle->width, handle->height);
	glClearColor(0.0f, 0.0f, 0.5f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	GLint extensions;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
	for (int i = 0; i < extensions; i++)
	{
		const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
		if (strcmp(ext, "GL_ANDROID_extension_pack_es31a") == 0)
		{
			DLOG("GL_ANDROID_extension_pack_es31a supported");
			return 0; // found
		}
	}
	ELOG("Android extension pack 1 support not found");
	return 0;
}

static void callback_draw(TOOLSTEST *handle)
{
	static int first = 1;
	GLint value;
	glGetIntegerv(GL_MAX_FRAGMENT_ATOMIC_COUNTER_BUFFERS, &value);
	assert(value >= 1);
	if (first) DLOG("GL_MAX_FRAGMENT_ATOMIC_COUNTER_BUFFERS = %d (req 1)", value);
	glGetIntegerv(GL_MAX_FRAGMENT_ATOMIC_COUNTERS, &value);
	assert(value >= 8);
	if (first) DLOG("GL_MAX_FRAGMENT_ATOMIC_COUNTERS = %d (req 8)", value);
	glGetIntegerv(GL_MAX_FRAGMENT_IMAGE_UNIFORMS, &value);
	assert(value >= 4);
	if (first) DLOG("GL_MAX_FRAGMENT_IMAGE_UNIFORMS = %d (req 4)", value);
	glGetIntegerv(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, &value);
	assert(value >= 4);
	if (first) DLOG("GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS = %d (req 4)", value);
	first = 0;
}

static void test_cleanup(TOOLSTEST *handle)
{
	// Nothing
}

int main(int argc, char** argv)
{
	return init(argc, argv, "extension_pack_es31a", callback_draw, setupGraphics, test_cleanup);
}
