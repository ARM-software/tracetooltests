// Test the KHR_debug extension

#include "gles_common.h"

static int setupGraphics(TOOLSTEST *handle)
{
	glViewport(0, 0, handle->width, handle->height);
	glClearColor(0.0f, 0.0f, 0.5f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	return 0;
}

static void debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
	ELOG("Message (khr_debug test): %s", message);
	if (type == GL_DEBUG_TYPE_ERROR)
	{
		abort();
	}
	if (strcmp((const char *)userParam, "PASSTHROUGH") != 0)
	{
		ELOG("User parameter string is wrong!");
		abort();
	}
}

// first frame render something, second frame verify it
static void callback_draw(TOOLSTEST *handle)
{
	glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "GROUP!");

	const char *txt = "PASSTHROUGH";
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(debug_callback, txt);
	void *param;
	glGetPointerv(GL_DEBUG_CALLBACK_FUNCTION, &param);
	// The two asserts below will fail during tracing, since we hijack the debug callback there!
	//assert(param == debug_callback);
	glGetPointerv(GL_DEBUG_CALLBACK_USER_PARAM, &param);
	//assert(param == txt);

	unsigned int ids[100];
	memset(ids, 0, sizeof(ids));
	glDebugMessageControl(GL_DEBUG_SOURCE_OTHER, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 100, ids, GL_TRUE);
	glDebugMessageCallback(NULL, NULL); // stop callback, start queueing up messages for later retrieval
	glDebugMessageControl(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER, GL_DEBUG_SEVERITY_LOW, 0, NULL, GL_TRUE);
	glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER, 0, GL_DEBUG_SEVERITY_LOW, -1, "Injected error");
	char log[255];
	memset(log, 0, sizeof(log));
	GLenum source;
	GLenum type;
	GLenum severity;
	unsigned int id;
	GLsizei size;
	unsigned int n = glGetDebugMessageLog(1, sizeof(log) - 1, &source, &type, &id, &severity, &size, log);
	if (n > 0)
	{
		DLOG("Retrieved message: %s", log);
	}
	else
	{
		ELOG("No errors found, not even our injected one!");
	}

	glDebugMessageCallback(debug_callback, txt); // re-enable error repoting, in case we get an error below...

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	const char *shadertext = "TEST_SHADER";
	glObjectLabel(GL_SHADER, vs, -1, shadertext);
	memset(log, 0, sizeof(log));
	glGetObjectLabel(GL_SHADER, vs, sizeof(log), NULL, log);
	assert(*log);
	if (*log)
	{
		assert(strcmp(log, shadertext) == 0);
		DLOG("Object label: %s", log);
	}
	else
	{
		ELOG("No object label!");
	}
	glDeleteShader(vs);

	GLsync s = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	const char *synctext = "TEST_SYNC_OBJECT";
	glObjectPtrLabel(s, -1, synctext);
	memset(log, 0, sizeof(log));
	glGetObjectPtrLabel(s, sizeof(log) - 1, NULL, log);
	assert(*log);
	if (*log)
	{
		assert(strcmp(log, synctext) == 0);
		DLOG("Sync object label: %s", log);
	}
	else
	{
		ELOG("No syn cobject label!");
	}
	glDeleteSync(s);

	glPopDebugGroup();
}

static void test_cleanup(TOOLSTEST *handle)
{
	// Nothing
}

int main(int argc, char** argv)
{
	return init(argc, argv, "khr_debug", callback_draw, setupGraphics, test_cleanup);
}
