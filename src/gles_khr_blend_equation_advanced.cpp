#include "gles_common.h"

const char *vertex_shader_source[] = GLSL_VS(
	in vec4 vPosition;
	out vec4 c;

	void main()
	{
		gl_Position = vec4((1.0 + float(gl_InstanceID) / 20.0) * vPosition.x, vPosition.y + float(gl_InstanceID) / 10.0, vPosition.z, vPosition.w);
		c = vPosition;
	}
);

const char *fragment_shader_source[] = GLSL_FS(
	in vec4 c;
	layout(blend_support_all_equations) out;
	out vec4 fragmentColor;

	void main()
	{
		fragmentColor = vec4(1.0f, 0.0f, 0.0f, 0.5f);
	}
);

static GLuint vs, fs, draw_program;

static int setupGraphics(TOOLSTEST *handle)
{
	// setup space
	glViewport(0, 0, handle->width, handle->height);

	// setup draw program
	draw_program = glCreateProgram();
	vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, vertex_shader_source, NULL);
	compile("vertex_shader_source", vs);
	fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, fragment_shader_source, NULL);
	compile("fragment_shader_source", fs);
	glAttachShader(draw_program, vs);
	glAttachShader(draw_program, fs);
	link_shader("draw_program", draw_program);
	glUseProgram(draw_program);

	return 0;
}

static const GLfloat gTriangleVertices[] = {
	0.0f,  0.5f,  0,
	-0.5f, -0.5f,  0,
	0.5f, -0.5f,  0
};

static int mode[] =
{
	GL_MULTIPLY_KHR,
	GL_SCREEN_KHR,
	GL_OVERLAY_KHR,
	GL_DARKEN_KHR,
	GL_LIGHTEN_KHR,
	GL_COLORDODGE_KHR,
	GL_COLORBURN_KHR,
	GL_HARDLIGHT_KHR,
	GL_SOFTLIGHT_KHR,
	GL_DIFFERENCE_KHR,
	GL_EXCLUSION_KHR,
	GL_HSL_HUE_KHR,
	GL_HSL_SATURATION_KHR,
	GL_HSL_COLOR_KHR,
	GL_HSL_LUMINOSITY_KHR
};

static void callback_draw(TOOLSTEST *handle)
{
	GLuint gvPositionHandle = glGetAttribLocation(draw_program, "vPosition");

	glClearColor(0.0f, 0.5f, 0.5f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	glEnable(GL_BLEND);
	glUseProgram(draw_program);

	glVertexAttribPointer(gvPositionHandle, 3, GL_FLOAT, GL_FALSE, 0, gTriangleVertices);
	glEnableVertexAttribArray(gvPositionHandle);

	for (unsigned i = 0; i < sizeof(mode) / sizeof(int); i++)
	{
		glBlendEquation(mode[i]);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBlendBarrier();
	}

	assert_fb(handle);
}

static void test_cleanup(TOOLSTEST *handle)
{
	glDeleteShader(vs);
	glDeleteShader(fs);
	glDeleteProgram(draw_program);
}

int main(int argc, char** argv)
{
	return init(argc, argv, "khr_blend_equation_advanced", callback_draw, setupGraphics, test_cleanup);
}
