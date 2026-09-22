#include "gles_common.h"

const char *vertex_shader_source[] = GLSL_VS(
	in vec2 position;
	in vec4 unused_color;
	in vec4 color;
	out vec4 pass_unused_color;
	out vec4 pass_color;
	void main()
	{
		pass_unused_color = unused_color;
		pass_color = color;
		gl_Position = vec4(position, 0.0, 1.0);
	}
);

const char *fragment_shader_source[] = GLSL_FS(
	in vec4 pass_unused_color;
	in vec4 pass_color;
	out vec4 frag_color;
	void main()
	{
		vec4 final_color = pass_unused_color;
		final_color = pass_color;
		frag_color = final_color;
	}
);

const float triangle_positions[] =
{
	 0.0f,  0.5f,
	-0.5f, -0.5f,
	 0.5f, -0.5f,
};

const float triangle_colors[] =
{
	0.0f, 1.0f, 0.0f, 1.0f,
	0.0f, 1.0f, 0.0f, 1.0f,
	0.0f, 1.0f, 0.0f, 1.0f,
};

const float unused_colors[] =
{
	1.0f, 0.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 0.0f, 1.0f,
};

static GLuint vertex_shader;
static GLuint fragment_shader;
static GLuint program;
static GLint position_location;
static GLint color_location;
static GLint unused_color_location;

static GLint lowest_free_location(GLint first, GLint second)
{
	GLint max_vertex_attribs = 0;
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attribs);
	for (GLint location = 0; location < max_vertex_attribs; ++location)
	{
		if (location != first && location != second)
		{
			return location;
		}
	}
	return -1;
}

static int setup_graphics(TOOLSTEST *handle)
{
	glViewport(0, 0, handle->width, handle->height);
	glClearColor(0.0f, 0.0f, 0.5f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	program = glCreateProgram();
	vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex_shader, 1, vertex_shader_source, NULL);
	compile("vertex_shader_source", vertex_shader);
	fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment_shader, 1, fragment_shader_source, NULL);
	compile("fragment_shader_source", fragment_shader);
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	link_shader("program", program);
	glUseProgram(program);

	position_location = glGetAttribLocation(program, "position");
	color_location = glGetAttribLocation(program, "color");
	unused_color_location = glGetAttribLocation(program, "unused_color");
	assert(position_location >= 0);
	assert(color_location >= 0);
	assert(position_location != color_location);

	if (unused_color_location < 0)
	{
		unused_color_location = lowest_free_location(position_location,
		                                             color_location);
	}
	assert(unused_color_location >= 0);
	assert(unused_color_location != position_location);
	assert(unused_color_location != color_location);

	glEnableVertexAttribArray(position_location);
	glEnableVertexAttribArray(unused_color_location);
	glEnableVertexAttribArray(color_location);
	glVertexAttribPointer(position_location, 2, GL_FLOAT, GL_FALSE, 0,
	                      triangle_positions);
	glVertexAttribPointer(unused_color_location, 4, GL_FLOAT, GL_FALSE, 0,
	                      unused_colors);
	glVertexAttribPointer(color_location, 4, GL_FLOAT, GL_FALSE, 0,
	                      triangle_colors);

	return 0;
}

static void callback_draw(TOOLSTEST *handle)
{
	glUseProgram(program);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	assert_fb(handle);
}

static void test_cleanup(TOOLSTEST *handle)
{
	(void)handle;
	glDisableVertexAttribArray(position_location);
	glDisableVertexAttribArray(unused_color_location);
	glDisableVertexAttribArray(color_location);
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);
	glDeleteProgram(program);
}

int main(int argc, char **argv)
{
	return init(argc, argv, "unused_vertex_attrib", callback_draw,
	            setup_graphics, test_cleanup);
}
