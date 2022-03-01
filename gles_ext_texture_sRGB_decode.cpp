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
	uniform sampler2D s_texture;

	void main()
	{
		fragmentColor = texture(s_texture, c.xy);
	}
);

static GLuint vs, fs, draw_program;
static GLuint g_textureId;

static GLubyte pixels[4 * 3] =
{
	16, 232, 0,
	48, 96, 64,
	128, 144, 192,
	255, 32, 204,
};

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
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_textureId);
	glUniform1i(glGetUniformLocation(draw_program, "s_texture"), 0);
	gvPositionHandle = glGetAttribLocation(draw_program, "vPosition");

	return 0;
}

static const int triangle_num = 4;
static const GLfloat gTriangleVertices[triangle_num][9] =
{
	{ -0.75f, 0.875f, 0, -0.875f, 0.125f, 0, -0.625f, 0.125f, 0 },
	{ 0.25f, 0.875f, 0, 0.125f, 0.125f, 0, 0.375f, 0.125f, 0 },
	{ -0.75f, -0.125f, 0, -0.875f, -0.875f, 0, -0.625f, -0.875f, 0 },
	{ 0.25f, -0.125f, 0, 0.125f, -0.875f, 0, 0.375f, -0.875f, 0 }
};

static const GLfloat gsRGBTriangleVertices[triangle_num][9] =
{
	{ -0.25f, 0.875f, 0, -0.375f, 0.125f, 0, -0.125f, 0.125f, 0 },
	{ 0.75f, 0.875f, 0, 0.625f, 0.125f, 0, 0.875f, 0.125f, 0 },
	{ -0.25f, -0.125f, 0, -0.375f, -0.875f, 0, -0.125f, -0.875f, 0 },
	{ 0.75f, -0.125f, 0, 0.625f, -0.875f, 0, 0.875f, -0.875f, 0 }
};

static void callback_draw(TOOLSTEST *handle)
{
	glClearColor(0.0f, 0.5f, 0.5f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SRGB_DECODE_EXT, GL_SKIP_DECODE_EXT);
	for (int i = 0; i < triangle_num; i++)
	{
		glUseProgram(draw_program);
		glVertexAttribPointer(gvPositionHandle, 3, GL_FLOAT, GL_FALSE, 0, gTriangleVertices[i]);
		glEnableVertexAttribArray(gvPositionHandle);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SRGB_DECODE_EXT, GL_DECODE_EXT);
	for (int i = 0; i < triangle_num; i++)
	{
		glUseProgram(draw_program);
		glVertexAttribPointer(gvPositionHandle, 3, GL_FLOAT, GL_FALSE, 0, gsRGBTriangleVertices[i]);
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
	return init(argc, argv, "ext_texture_sRGB_decode", callback_draw, setupGraphics, test_cleanup);
}
