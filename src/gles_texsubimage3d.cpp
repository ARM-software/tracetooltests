#include "gles_common.h"
#include <vector>

static GLuint maintex = 0;
static GLenum internalformat = GL_RGB8;
static GLenum format = GL_RGB;
static GLenum type = GL_UNSIGNED_BYTE;
static GLuint bytesperpixel = 3;
static GLsizei dim = 512;
static std::vector<char> buffer[2];
static int upload_variant = 0;

static void our_usage()
{
	printf("-T/--texture-size N    Initial texture size (default %d)\n", dim);
	printf("-f/--format-variant N  Texture format (default 0)\n");
	printf("\t0 - GL_RGB8\n");
	printf("\t1 - GL_RGBA8\n");
	printf("\t2 - GL_R8\n");
	printf("-u/--upload-variant N  Upload variant (default %d)\n", upload_variant);
	printf("\t0 - glTexStorage3D\n");
	printf("\t1 - glTexImage3D\n");
}

static int setupGraphics(TOOLSTEST *handle)
{
	buffer[0].resize(dim * dim * dim * bytesperpixel);
	buffer[1].resize(dim * dim * dim * bytesperpixel);
	memset(buffer[0].data(), 0xbe, buffer[0].size());
	memset(buffer[1].data(), 0xef, buffer[1].size());
	assert(buffer[0].size() > 0);
	assert(buffer[1].size() > 0);
	glGenTextures(1, &maintex);
	glBindTexture(GL_TEXTURE_3D, maintex);
	if (upload_variant == 0)
	{
		glTexStorage3D(GL_TEXTURE_3D, 1, internalformat, dim, dim, dim);
	}
	else
	{
		glTexImage3D(GL_TEXTURE_3D, 0, internalformat, dim, dim, dim, 0, format, type, nullptr);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
	}
	return 0;
}

static void callback_draw(TOOLSTEST *handle)
{
	int i = 0;
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	GLuint curr = dim;
	for (; curr >= 16; curr /= 2)
	{
		glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, curr, curr, curr, format, type, buffer[i].data());
		i++; if (i == 2) i = 0;
	}
}

static void test_cleanup(TOOLSTEST *handle)
{
	glDeleteTextures(1, &maintex);
}

static bool test_cmdopt(int& i, int argc, char** argv)
{
	if (match(argv[i], "-T", "--texture-size"))
	{
		dim = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-u", "--upload-variant"))
	{
		upload_variant = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-f", "--format-variant"))
	{
		unsigned format_variant = get_arg(argv, ++i, argc);
		switch (format_variant)
		{
		case 0: break; // use defaults
		case 1:
			internalformat = GL_RGBA8;
			format = GL_RGBA;
			type = GL_UNSIGNED_BYTE;
			bytesperpixel = 4;
			break;
		case 2:
			internalformat = GL_R8;
			format = GL_RED;
			type = GL_UNSIGNED_BYTE;
			bytesperpixel = 1;
			break;
		default: return false;
		}
		return true;
	}
	return false;
}

int main(int argc, char** argv)
{
	TOOLSTEST_INIT initparam;
	initparam.name = "gles_texsubimage3d";
	initparam.swap = callback_draw;
	initparam.init = setupGraphics;
	initparam.done = test_cleanup;
	initparam.usage = our_usage;
	initparam.cmdopt = test_cmdopt;
	return init(argc, argv, initparam);
}
