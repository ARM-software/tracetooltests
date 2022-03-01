// Test usage of an uninitialized texture

#include "gles_common.h"

static GLuint gvPositionHandle;

const char *vertex_shader_source[] = GLSL_VS(
	in vec4 vPosition;
	out vec4 c;

	void main()
	{
		gl_Position = vPosition;
		c = vec4((vPosition.xy + vec2(1.0f, 1.0f)) / vec2(2.0f, 2.0f), 0.0f, 1.0f);
	}
);

const char *fragment_shader_source[] = GLSL_FS(
	in vec4 c;
	out vec4 fragmentColor;
	uniform highp isampler2D s_texture;

	void main()
	{
		fragmentColor = vec4(texture(s_texture, c.xy)) * 1.0f / 255.0f;
	}
);

static GLuint vs, fs, draw_program;
static GLuint g_textureId;

static int setupGraphics(TOOLSTEST *handle)
{
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

	glGenTextures(1, &g_textureId);
	glBindTexture(GL_TEXTURE_2D, g_textureId);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUniform1i(glGetUniformLocation(draw_program, "s_texture"), 0);

	gvPositionHandle = glGetAttribLocation(draw_program, "vPosition");

	return 0;
}

static const int triangle_num = 4;
static const GLfloat gTriangleVertices[triangle_num][9] =
{
	{ -0.5f, 0.875f, 0, -0.875f, 0.125f, 0, -0.125f, 0.125f, 0 },
	{ 0.5f, 0.875f, 0, 0.125f, 0.125f, 0, 0.875f, 0.125f, 0 },
	{ -0.5f, -0.125f, 0, -0.875f, -0.875f, 0, -0.125f, -0.875f, 0 },
	{ 0.5f, -0.125f, 0, 0.125f, -0.875f, 0, 0.875f, -0.875f, 0 }
};

static void callback_draw(TOOLSTEST *handle)
{
	glClearColor(0.0f, 0.5f, 0.5f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	for (int i = 0; i < triangle_num; i++)
	{
		glUseProgram(draw_program);
		glVertexAttribPointer(gvPositionHandle, 3, GL_FLOAT, GL_FALSE, 0, gTriangleVertices[i]);
		glEnableVertexAttribArray(gvPositionHandle);
		glDrawArrays(GL_TRIANGLES, 0, 3);
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
	printf("Note: This test is deliberately erroneous!\n");
	return init(argc, argv, "uninit_texture_2", callback_draw, setupGraphics, test_cleanup);
}
