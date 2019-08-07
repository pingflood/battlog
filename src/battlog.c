#include <SDL/SDL.h>
// #include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include "font.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>

#include <sys/ioctl.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/fb.h>

#include <stdio.h>
#include <stdlib.h>

#include <time.h>
#include <math.h>

#include <linux/limits.h>

static const int SDL_WAKEUPEVENT = SDL_USEREVENT+1;

#ifndef TARGET_RETROFW
	#define system(x) printf(x); printf("\n")
#endif

#ifndef TARGET_RETROFW
	#define DBG(x) printf("%s:%d %s %s\n", __FILE__, __LINE__, __func__, x);
#else
	#define DBG(x)
#endif


#define WIDTH  320
#define HEIGHT 240

#define GPIO_BASE		0x10010000
#define PAPIN			((0x10010000 - GPIO_BASE) >> 2)
#define PBPIN			((0x10010100 - GPIO_BASE) >> 2)
#define PCPIN			((0x10010200 - GPIO_BASE) >> 2)
#define PDPIN			((0x10010300 - GPIO_BASE) >> 2)
#define PEPIN			((0x10010400 - GPIO_BASE) >> 2)
#define PFPIN			((0x10010500 - GPIO_BASE) >> 2)

#define BTN_X			SDLK_SPACE
#define BTN_A			SDLK_LCTRL
#define BTN_B			SDLK_LALT
#define BTN_Y			SDLK_LSHIFT
#define BTN_L			SDLK_TAB
#define BTN_R			SDLK_BACKSPACE
#define BTN_START		SDLK_RETURN
#define BTN_SELECT		SDLK_ESCAPE
#define BTN_BACKLIGHT	SDLK_3
#define BTN_POWER		SDLK_END
#define BTN_UP			SDLK_UP
#define BTN_DOWN		SDLK_DOWN
#define BTN_LEFT		SDLK_LEFT
#define BTN_RIGHT		SDLK_RIGHT

const int	HAlignLeft		= 1,
			HAlignRight		= 2,
			HAlignCenter	= 4,
			VAlignTop		= 8,
			VAlignBottom	= 16,
			VAlignMiddle	= 32;

SDL_RWops *rw;
TTF_Font *font = NULL;
SDL_Surface *ScreenSurface = NULL;

SDL_Color txtColor = {200, 200, 220};
SDL_Color titleColor = {200, 200, 0};
SDL_Color subTitleColor = {0, 200, 0};
SDL_Color powerColor = {200, 0, 0};

SDL_TimerID timer_running = NULL;

SDL_Rect limits;

int running = 0;

volatile uint32_t running_tick = 0;

int32_t log_idx = 0;
char log_csv[PATH_MAX] = "battery0.csv";
char log_png[PATH_MAX] = "battery0.png";

static char buf[1024];

uint32_t *mem;

extern uint8_t rwfont[];

uint8_t file_exists(const char path[512]) {
	struct stat s;
	return !!(stat(path, &s) == 0 && s.st_mode & S_IFREG); // exists and is file
}

char *ms2hms(uint32_t t, uint32_t mult) {
	static char buf[10];

	t = t / mult;
	int s = (t % 60);
	int m = (t % 3600) / 60;
	int h = (t % 86400) / 3600;
	sprintf(buf, "%02d:%02d:%02d", h, m, s);
	return buf;
};

int draw_text(int x, int y, const char buf[64], SDL_Color txtColor, int align) {
	DBG("");

	SDL_Surface *msg = TTF_RenderText_Blended(font, buf, txtColor);

	if (align & HAlignCenter) {
		x -= msg->w / 2;
	} else if (align & HAlignRight) {
		x -= msg->w;
	}

	if (align & VAlignMiddle) {
		y -= msg->h / 2;
	} else if (align & VAlignTop) {
		y -= msg->h;
	}

	SDL_Rect rect;
	rect.x = x;
	rect.y = y;
	rect.w = msg->w;
	rect.h = msg->h;
	SDL_BlitSurface(msg, NULL, ScreenSurface, &rect);
	SDL_FreeSurface(msg);
	return msg->w;
}

void draw_background(const char buf[64]) {
	DBG("");
	SDL_Rect rect;
	rect.w = WIDTH;
	rect.h = HEIGHT;
	rect.x = 0;
	rect.y = 0;
	SDL_FillRect(ScreenSurface, &rect, SDL_MapRGB(ScreenSurface->format, 0, 0, 0));

	// title
	draw_text(310, 4, "RetroFW", titleColor, VAlignBottom | HAlignRight);
	draw_text(10, 4, buf, titleColor, VAlignBottom);
}

void draw_axis(SDL_Rect rect) {
	DBG("");
	SDL_Rect rect2;
	rect2.w = rect.w - 1;
	rect2.h = rect.h - 1;
	rect2.x = rect.x + 1;
	rect2.y = rect.y;
	SDL_FillRect(ScreenSurface, &rect, SDL_MapRGB(ScreenSurface->format, 0xff, 0xff, 0xff));
	SDL_FillRect(ScreenSurface, &rect2, SDL_MapRGB(ScreenSurface->format, 0, 0, 0));
}

void draw_point(uint32_t x, uint32_t y) {
	// DBG("");
	SDL_Rect rect;
	rect.w = 2;
	rect.h = 2;
	rect.x = x;
	rect.y = y - 2;
	SDL_FillRect(ScreenSurface, &rect, SDL_MapRGB(ScreenSurface->format, 0xff, 0xff, 0xff));
}

float map(int32_t x, int32_t xn, int32_t xx, int32_t rn, int32_t rx) {
	if (xx == xn) return -1;
	return (float)( x - xn ) * (float)(rx - rn) / (float)(xx - xn) + rn;
}

void quit(int err) {
	DBG("");
	system("sync");
	if (font) TTF_CloseFont(font);
	font = NULL;
	SDL_Quit();
	TTF_Quit();
	exit(err);
}

void write_battery_log() {
	DBG("");
	FILE *f = fopen("/proc/jz/battery", "r");
	if (f) {
		fgets(buf, sizeof(buf), f);
		fclose(f);
	}
	uint32_t y = atoi(buf);
	if (y < 1 || y > 5000) y = 5000;

	FILE *log = fopen(log_csv, "a");
	if (log) {
		fprintf(log, "%d,%d\n", running_tick, y);
		fclose(log);
	}
}

int set_log_idx(int step) {
	log_idx += step;
	if (log_idx < 0) {
		log_idx = 0;
		return 0;
	}

	sprintf(log_csv, "battery%d.csv", log_idx);
	sprintf(log_png, "battery%d.png", log_idx);
	return 1;
}

void new_battery_log() {
	if (file_exists("battery.csv")) {
		int x = 1;
		do {
			sprintf(buf, "battery%d.csv", x);
			x++;
		}
		while (file_exists(buf));
		rename("battery.csv", buf);
	}
}

static int cpu_load(void *ptr)
{
	time_t t;
	srand((unsigned) time(&t));
	while (running)
	{
		float x = (float)rand() / (float)(RAND_MAX) * 5;
		x *= sin(x) * atan(x) * tanh(x) * sqrt(x);
	}
	return 0;
}

uint32_t update_time(uint32_t interval, void *repeat) {
	SDL_Rect rect;
	rect.w = 70;
	rect.h = 17;
	rect.x = limits.x + limits.w - 70;
	rect.y = limits.y + limits.h;
	SDL_FillRect(ScreenSurface, &rect, SDL_MapRGB(ScreenSurface->format, 0, 0, 0));

	sprintf(buf, "%s", ms2hms(running_tick, 1));
	draw_text(limits.x + limits.w, limits.y + limits.h, buf, txtColor, VAlignBottom | HAlignRight);

	SDL_Flip(ScreenSurface);

	if (running_tick % 60 == 0) {
		write_battery_log();
		SDL_Event user_event;
		user_event.type = SDL_USEREVENT;
		SDL_PushEvent(&user_event);
	}
	running_tick++;

	return interval;
}

int main(int argc, char* argv[]) {
	DBG("");
	signal(SIGINT, &quit);
	signal(SIGSEGV,&quit);
	signal(SIGTERM,&quit);

	char title[64] = "";
	uint8_t *keys = SDL_GetKeyState(NULL);
	uint8_t nextline = 24;

	sprintf(title, "BATTERY LOGGER");

	printf("%s\n", title);

	SDL_Rect rect = {0};
	SDL_Event event;

	setenv("SDL_NOMOUSE", "1", 1);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		printf("Could not initialize SDL: %s\n", SDL_GetError());
		return -1;
	}
	SDL_PumpEvents();
	SDL_ShowCursor(0);

	ScreenSurface = SDL_SetVideoMode(WIDTH, HEIGHT, 16, SDL_SWSURFACE);

	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

	if (TTF_Init() == -1) {
		printf("failed to TTF_Init\n");
		return -1;
	}
	rw = SDL_RWFromMem(rwfont, sizeof(rwfont));
	font = TTF_OpenFontRW(rw, 1, 8);
	TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
	TTF_SetFontOutline(font, 0);

	int loop = 1;
	do {
		// background
		draw_background(title);

		if (running) {
			SDL_Rect rect;
			rect.w = 320;
			rect.h = 20;
			rect.x = 0;
			rect.y = 225;
			SDL_FillRect(ScreenSurface, &rect, SDL_MapRGB(ScreenSurface->format, running * 150, 0, 0));
			draw_text(310, 230, "START: Stop", txtColor, VAlignMiddle | HAlignRight);
		} else if (!file_exists(log_csv)) {
			draw_text(310, 230, "SELECT: Exit     START: Start", txtColor, VAlignMiddle | HAlignRight);
		} else {
			draw_text(310, 230, "SELECT: Exit", txtColor, VAlignMiddle | HAlignRight);
		}

		sprintf(buf, "< > %-20s", log_csv);
		draw_text(10, 230, buf, txtColor, VAlignMiddle | HAlignLeft);

		if (!file_exists(log_csv)) {
			int ln = 18;
			draw_text(10, ln += 16, "LEFT/RIGHT: Select the log file", txtColor, VAlignMiddle | HAlignLeft);
			draw_text(10, ln += 16, "START:  Start/stop logging", txtColor, VAlignMiddle | HAlignLeft);
			draw_text(10, ln += 16, "SELECT: Exit", txtColor, VAlignMiddle | HAlignLeft);
			draw_text(10, ln += 24, "With the battery fully charged start logging", txtColor, VAlignMiddle | HAlignLeft);
			draw_text(10, ln += 16, "and let it discharge completely.", txtColor, VAlignMiddle | HAlignLeft);
			sprintf(buf, "The log will be saved in %s.", log_csv);
			draw_text(10, ln += 24, buf, txtColor, VAlignMiddle | HAlignLeft);

		} else {
			uint32_t x, y, tw;
			uint32_t xn = 0xFFFFFFFF;
			uint32_t xx = 0;
			uint32_t yn = 0xFFFFFFFF;
			uint32_t yx = 0;
			uint32_t lines = 0;
	
			// check limits
			char *line = NULL;
			size_t len = 0;
			ssize_t read;
	
			FILE *f = fopen(log_csv, "r");
			if (f) {
				while ((read = getline(&line, &len, f)) != -1) {
					char *pt = strtok(line,",");
					int x = atoi(pt);
					pt = strtok(NULL, ",");
					int y = atoi(pt);
					if (x > xx) xx = x;
					if (x < xn) xn = x;
					if (y > yx) yx = y;
					if (y < yn) yn = y;
					lines++;
				}
				fclose(f);
			}
	
			if (xx == xn) xx = xn + 10;
			if (yx == yn) yx = yn + 10;
	
			sprintf(buf, "%0.2f", (float)yx / 1000);
			tw = draw_text(320, 0, buf, txtColor, VAlignTop);
	
			limits.x = 10 + tw;
			limits.y = 50;
			limits.w = 300 - tw;
			limits.h = 130;
	
			draw_axis(limits);
	
			sprintf(buf, "%0.1f", (float)yx / 1000);
			draw_text(limits.x - 4, limits.y, buf, txtColor, VAlignMiddle | HAlignRight);
	
			sprintf(buf, "%0.1f", (float)yn / 1000);
			draw_text(limits.x - 4, limits.y + limits.h, buf, txtColor, VAlignTop | HAlignRight);
	
			sprintf(buf, "%s", ms2hms(xn, 1));
			draw_text(limits.x, limits.y + limits.h, buf, txtColor, VAlignBottom | HAlignLeft);
	
			sprintf(buf, "%s", ms2hms(xx, 1));
			tw = draw_text(limits.x + limits.w, limits.y + limits.h, buf, txtColor, VAlignBottom | HAlignRight);
	
			f = fopen(log_csv, "r");
			if (f) {
				uint32_t interval = 1 + lines / 5;
				uint32_t step = 0;
				while ((read = getline(&line, &len, f)) != -1) {
					char *pt = strtok (line,",");
					uint32_t x = atoi(pt);
					pt = strtok (NULL, ",");
					uint32_t y = atoi(pt);

					uint32_t xp = map(x, xn, xx, limits.x, limits.x + limits.w);
					uint32_t yp = map(y, yx, yn, limits.y, limits.y + limits.h);

					draw_point(xp, yp);

					SDL_Rect rect;
					rect.w = 70;
					rect.h = 36;
					rect.x = limits.x + limits.w - 70;
					rect.y = limits.y + limits.h;
					SDL_FillRect(ScreenSurface, &rect, SDL_MapRGB(ScreenSurface->format, 0, 0, 0));

					sprintf(buf, "%s", ms2hms(x, 1));
					draw_text(limits.x + limits.w, limits.y + limits.h, buf, txtColor, VAlignBottom | HAlignRight);

					sprintf(buf, "Value: %0.1f V", (float)y / 1000);
					draw_text(limits.x + limits.w, limits.y + limits.h + 26, buf, txtColor, VAlignMiddle | HAlignRight);

					step++;
					if (step >= interval) {
						step = 0;
						SDL_Delay(1);
						SDL_Flip(ScreenSurface);
					}
				}
				fclose(f);
				while (SDL_PollEvent(&event)) { SDL_PumpEvents(); }
			}
		}
		SDL_Flip(ScreenSurface);

		if (!running && file_exists(log_csv) && !file_exists(log_png)) {
			sprintf(buf, "fbgrab %s; sync", log_png);
			system(buf);
		}

		while (SDL_WaitEvent(&event)) {
			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == BTN_START) {
				if (running) {
					running = !running;
					SDL_RemoveTimer(timer_running);
					break;
				} else if (!file_exists(log_csv)) {
					system("echo 100 > /proc/jz/lcd_backlight");
					running = !running;
					running_tick = 0;
					update_time(0, NULL);
					timer_running = SDL_AddTimer(1e3, update_time, NULL);
					SDL_Thread *thread = SDL_CreateThread(cpu_load, (void *)NULL);
					break;
				}
			}

			if (event.type == SDL_KEYDOWN && !running) {
				if (event.key.keysym.sym == BTN_POWER || event.key.keysym.sym == BTN_SELECT) {
					loop = 0;
					break;
				}
				if (event.key.keysym.sym == BTN_LEFT && set_log_idx(-1)) break;
				if (event.key.keysym.sym == BTN_RIGHT && set_log_idx(1)) break;
			}

			if (event.type == SDL_USEREVENT && running) {
				break;
			}
		}
	} while (loop);
	quit(0);
	return 0;
}
