#include "util.h"

#include <termios.h>
#include <unistd.h>
#include <string.h>

#if defined(_GNU_SOURCE) || defined(__BIONIC__)
#include <pthread.h>
#else
#include <sys/prctl.h>
#endif

#ifdef SDL
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#endif

uint_fast8_t p__debug_level = 1;

void set_thread_name(const char* name)
{
	// "length is restricted to 16 characters, including the terminating null byte"
	// http://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
	assert(strlen(name) <= 15);
#if defined(_GNU_SOURCE) || defined(__BIONIC__)
	pthread_setname_np(pthread_self(), name);
#else
	prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
#endif
}

char keypress()
{
	char ch = 0;
#if SDL
	SDL_Event event;
	while (true)
	{
		bool any = SDL_PollEvent(&event);
		if (any && event.type == SDL_KEYDOWN && event.key.keysym.scancode < 0x80 && event.key.keysym.scancode > 0) return (char)event.key.keysym.scancode;
		else if (any && event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) return 'q';
	}
#else
	struct termios argin;
	struct termios argout;
	tcgetattr(0, &argin);
	argout = argin;
	argout.c_lflag &= ~(ICANON);
	argout.c_iflag &= ~(ICRNL);
	argout.c_oflag &= ~(OPOST);
	argout.c_cc[VMIN] = 1;
	argout.c_cc[VTIME] = 0;
	tcsetattr(0, TCSADRAIN, &argout);
	size_t r = read(0, &ch, 1);
	(void)r; // make compiler happy
	tcsetattr(0, TCSADRAIN, &argin);
#endif
	return ch;
}

bool match(const char* in, const char* short_form, const char* long_form)
{
	if (strcmp(in, short_form) == 0 || strcmp(in, long_form) == 0)
	{
		return true;
	}
	return false;
}

int get_arg(char** in, int i, int argc)
{
	if (i == argc)
	{
		ELOG("Missing command line parameter\n");
		exit(-1);
	}
	return atoi(in[i]);
}
