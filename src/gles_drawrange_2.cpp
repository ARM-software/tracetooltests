#include "gles_common.h"

const char *vertex_shader_source[] = GLSL_VS(
	in vec4 a_v4Position;
	in vec4 a_v4FillColor;
	out vec4 v_v4FillColor;
	void main()
	{
		v_v4FillColor = a_v4FillColor;
		gl_Position = a_v4Position;
	}
);

const char *fragment_shader_source[] = GLSL_FS(
	in vec4 v_v4FillColor;
	out vec4 fragColor;
	void main()
	{
	        fragColor = v_v4FillColor;
	}
);

const GLfloat data[] =
{
	// X	Y	R	G	B	A
	0.0f,	0.5f,	1.0f,	0.0f,	1.0f,	1.0f,
	-0.5f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,
	0.5f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,

	0.15f,	0.5f,	1.0f,	0.0f,	1.0f,	1.0f,
	-0.35f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,
	0.65f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,

	0.30f,	0.5f,	1.0f,	0.0f,	1.0f,	1.0f,
	-0.20f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,
	0.8f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,

	0.45f,	0.5f,	1.0f,	0.0f,	1.0f,	1.0f,
	-0.05f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,
	0.95f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,

	0.60f,	0.5f,	1.0f,	0.0f,	1.0f,	1.0f,
	0.10f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,
	1.10f,	-0.5f,	0.0f,	1.0f,	0.0f,	1.0f,
};

const unsigned short indices[] = // 5 triangles
{
      0, 1, 2,
      3, 4, 5,
      6, 7, 8,
      9, 10, 11,
      12, 13, 14
};

static GLuint vs, fs, draw_program, iLocPosition, iLocFillColor;

static int setupGraphics(TOOLSTEST *handle)
{
	// setup space
	glViewport(0, 0, handle->width, handle->height);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0.0f, 0.0f, 0.5f, 1.0f);
	glClearDepthf(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

	// setup buffers
	iLocPosition = glGetAttribLocation(draw_program, "a_v4Position");
	iLocFillColor = glGetAttribLocation(draw_program, "a_v4FillColor");
	glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 24, data);
	glVertexAttribPointer(iLocFillColor, 4, GL_FLOAT, GL_TRUE, 24, (data + 2));
	glEnableVertexAttribArray(iLocFillColor);
	glEnableVertexAttribArray(iLocPosition);

	return 0;
}

static void callback_draw(TOOLSTEST *handle)
{
	glDrawRangeElements(GL_TRIANGLES, 0, 14, 5, GL_UNSIGNED_SHORT, indices);
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
	return init(argc, argv, "drawrange_2", callback_draw, setupGraphics, test_cleanup);
}
