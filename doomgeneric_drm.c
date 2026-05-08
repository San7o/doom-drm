//doomgeneric for linux DRM

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"
#include "drm_driver.h"

#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <termios.h>
#include <linux/input.h>

/* Number of milliseconds elapsed since initialization */
uint32_t time_ms = 0;
struct timespec time_now;
struct timespec prev_time;

#define KEYQUEUE_SIZE 16

struct key_data {
  bool pressed;
  unsigned short key;
};

static struct key_data s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static unsigned char convertToDoomKey(unsigned int key){
  switch (key)
    {
    case KEY_ENTER:
      key = DOOM_KEY_ENTER;
      break;
    case KEY_ESC:
      key = DOOM_KEY_ESCAPE;
      break;
    case KEY_LEFT:
      key = DOOM_KEY_LEFTARROW;
      break;
    case KEY_RIGHT:
      key = DOOM_KEY_RIGHTARROW;
      break;
    case KEY_UP:
      key = DOOM_KEY_UPARROW;
      break;
    case KEY_DOWN:
      key = DOOM_KEY_DOWNARROW;
      break;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
      key = DOOM_KEY_FIRE;
      break;
    case KEY_SPACE:
      key = DOOM_KEY_USE;
      break;
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
      key = DOOM_KEY_RSHIFT;
      break;
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
      key = DOOM_KEY_LALT;
      break;
    case KEY_F2:
      key = DOOM_KEY_F2;
      break;
    case KEY_F3:
      key = DOOM_KEY_F3;
      break;
    case KEY_F4:
      key = DOOM_KEY_F4;
      break;
    case KEY_F5:
      key = DOOM_KEY_F5;
      break;
    case KEY_F6:
      key = DOOM_KEY_F6;
      break;
    case KEY_F7:
      key = DOOM_KEY_F7;
      break;
    case KEY_F8:
      key = DOOM_KEY_F8;
      break;
    case KEY_F9:
      key = DOOM_KEY_F9;
      break;
    case KEY_F10:
      key = DOOM_KEY_F10;
      break;
    case KEY_F11:
      key = DOOM_KEY_F11;
      break;
    case KEY_EQUAL:
    case KEY_KPPLUS:
      key = DOOM_KEY_EQUALS;
      break;
    case KEY_MINUS:
      key = DOOM_KEY_MINUS;
      break;
    case KEY_A:
      key = 'a';
      break;
    case KEY_B:
      key = 'b';
      break;
    case KEY_C:
      key = 'c';
      break;
    case KEY_D:
      key = 'd';
      break;
    case KEY_E:
      key = 'e';
      break;
    case KEY_F:
      key = 'f';
      break;
    case KEY_G:
      key = 'g';
      break;
    case KEY_H:
      key = 'h';
      break;
    case KEY_I:
      key = 'i';
      break;
    case KEY_J:
      key = 'j';
      break;
    case KEY_K:
      key = 'k';
      break;
    case KEY_L:
      key = 'l';
      break;
    case KEY_M:
      key = 'm';
      break;
    case KEY_N:
      key = 'n';
      break;
    case KEY_O:
      key = 'o';
      break;
    case KEY_P:
      key = 'p';
      break;
    case KEY_Q:
      key = 'q';
      break;
    case KEY_R:
      key = 'r';
      break;
    case KEY_S:
      key = 's';
      break;
    case KEY_T:
      key = 't';
      break;
    case KEY_U:
      key = 'u';
      break;
    case KEY_V:
      key = 'v';
      break;
    case KEY_W:
      key = 'w';
      break;
    case KEY_X:
      key = 'x';
      break;
    case KEY_Y:
      key = 'y';
      break;
    case KEY_Z:
      key = 'z';
      break;
    case KEY_0:
      key = '0';
      break;
    case KEY_1:
      key = '1';
      break;
    case KEY_2:
      key = '2';
      break;
    case KEY_3:
      key = '3';
      break;
    case KEY_4:
      key = '4';
      break;
    case KEY_5:
      key = '5';
      break;
    case KEY_6:
      key = '6';
      break;
    case KEY_7:
      key = '7';
      break;
    case KEY_8:
      key = '8';
      break;
    case KEY_9:
      key = '9';
      break;
    default:
      break;
    }

  return key;
}

static void addKeyToQueue(int pressed, char keyCode){
  unsigned char key = convertToDoomKey(keyCode);

  struct key_data keyData = {
    .pressed = pressed,
    .key = key,
  };

  s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
  s_KeyQueueWriteIndex++;
  s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}

static void handleKeyInput() {

  struct pollfd pollfds[2];
  pollfds[0].fd     = drm_driver.fd;
  pollfds[0].events = POLLIN;
  pollfds[1].fd     = drm_driver.input_fd;
  pollfds[1].events = POLLIN;

	// Incredibly inaccurate, but it doesn't really matter for this example
  int ret = poll(pollfds, 2, 0);
  if (ret < 0 && errno != EAGAIN) {
    perror("poll");
    return;
  }

  /* Handle DRM Events */
  if (pollfds[0].revents & POLLIN) {
    drmEventContext context = {
      .version = DRM_EVENT_CONTEXT_VERSION,
      .page_flip_handler = page_flip_handler,
    };

    if (drmHandleEvent(drm_driver.fd, &context) < 0) {
      perror("drmHandleEvent");
      return;
    }
  }

  /* Handle Keyboard Hits */
  if (pollfds[1].revents & POLLIN) {
    struct input_event ev;
    
    while (read(drm_driver.input_fd, &ev, sizeof(ev)) > 0) {
      if (ev.type == EV_KEY) {
        addKeyToQueue((ev.value > 0), ev.code);
      }
    }
  }
}

/* Doom interface */

void DG_Init() {}

void DG_DrawFrame()
{
  drm_driver_draw(DG_ScreenBuffer);
}

void DG_SleepMs(uint32_t ms)
{
  struct timespec duration = {
    .tv_sec = 0,
    .tv_nsec = ms * 1000000,
  };
  nanosleep(&duration, NULL);
}

uint32_t DG_GetTicksMs()
{
  // Update time
  clock_gettime(CLOCK_MONOTONIC, &time_now);
  uint32_t delta_time_ms = (time_now.tv_sec - prev_time.tv_sec) * 1000
    + (time_now.tv_nsec - prev_time.tv_nsec) / 1000000;
  prev_time = time_now;
  time_ms += delta_time_ms;

  return time_ms;
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
  handleKeyInput();

  if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex){
    //key queue is empty
    return 0;
  } else {
    struct key_data keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex++;
    s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

    *pressed = keyData.pressed;
    *doomKey = keyData.key;

    return 1;
  }

  return 0;
}

void DG_SetWindowTitle(const char * title)
{
  (void) title;
}

int main(int argc, char **argv)
{
    struct termios oldt, newt;
    time_ms = 0;

    /* Set the terminal in raw mode */
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    if (!drm_driver_init()) {
      tcsetattr(STDIN_FILENO, TCSANOW, &oldt);      
      return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &prev_time);

    doomgeneric_Create(argc, argv);

    while (1)
    {
      doomgeneric_Tick();
    }

    drm_driver_cleanup();

    /* Restore original terminal */
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return 0;
}
