#include <ndl.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

static int has_nwm = 0;
static uint32_t *canvas;
static int fbfd = -1;
static FILE *evtdev;

typedef struct {
  uint32_t *pixels;
  int x, y, w, h;
} NDL_FbBlitReq;

static void get_display_info();
static int canvas_w, canvas_h, screen_w, screen_h, pad_x, pad_y;
static int logical_w, logical_h;

int NDL_OpenDisplay(int w, int h) {
  if (canvas) {
    NDL_CloseDisplay();
  }

  logical_w = w;
  logical_h = h;

  if (getenv("NWM_APP")) {
    has_nwm = 1;
  } else {
    has_nwm = 0;
  }

  if (has_nwm) {
    canvas_w = w;
    canvas_h = h;
    canvas = calloc((size_t)w * h, sizeof(uint32_t));
    assert(canvas);
    printf("\033[X%d;%ds", w, h); fflush(stdout);
    evtdev = stdin;
  } else {
    get_display_info();
    assert(screen_w >= logical_w);
    assert(screen_h >= logical_h);
    canvas_w = logical_w;
    canvas_h = logical_h;
    pad_x = (screen_w - logical_w) / 2;
    pad_y = (screen_h - logical_h) / 2;
    canvas = calloc((size_t)canvas_w * canvas_h, sizeof(uint32_t));
    assert(canvas);
    fbfd = open("/dev/fb", O_WRONLY); assert(fbfd >= 0);
    evtdev = fopen("/dev/events", "r"); assert(evtdev);
  }
  return 0;
}

int NDL_CloseDisplay() {
  if (canvas) {
    free(canvas);
    canvas = NULL;
  }
  if (fbfd >= 0) {
    close(fbfd);
  }
  if (evtdev != NULL && evtdev != stdin) {
    fclose(evtdev);
  }
  fbfd = -1;
  evtdev = NULL;
  return 0;
}

int NDL_DrawRect(uint32_t *pixels, int x, int y, int w, int h) {
  if (has_nwm) {
    for (int i = 0; i < h; i ++) {
      printf("\033[X%d;%d", x, y + i);
      for (int j = 0; j < w; j ++) {
        putchar(';');
        fwrite(&pixels[i * w + j], 1, 4, stdout);
      }
      printf("d\n");
    }
  } else {
    for (int i = 0; i < h; i ++) {
      memcpy(&canvas[(i + y) * canvas_w + x],
          &pixels[i * w], (size_t)w * sizeof(uint32_t));
    }
  }
  return 0;
}

int NDL_BlitFrame(uint32_t *pixels, int w, int h) {
  assert(pixels);
  if (has_nwm || w != canvas_w || h != canvas_h) {
    NDL_DrawRect(pixels, 0, 0, w, h);
    return NDL_Render();
  }

  NDL_FbBlitReq req = {
    .pixels = pixels,
    .x = pad_x,
    .y = pad_y,
    .w = w,
    .h = h,
  };
  ssize_t ret = write(fbfd, &req, sizeof(req));
  assert(ret == (ssize_t)sizeof(req));
  return 0;
}

int NDL_Render() {
  if (has_nwm) {
    fflush(stdout);
  } else {
    NDL_FbBlitReq req = {
      .pixels = canvas,
      .x = pad_x,
      .y = pad_y,
      .w = canvas_w,
      .h = canvas_h,
    };
    ssize_t ret = write(fbfd, &req, sizeof(req));
    assert(ret == (ssize_t)sizeof(req));
  }
  return 0;
}

#define keyname(k) #k,

static const char *keys[] = {
  "NONE",
  _KEYS(keyname)
};

#define numkeys ( sizeof(keys) / sizeof(keys[0]) )

int NDL_WaitEvent(NDL_Event *event) {
  char buf[256];

  while (1) {
    char *p = buf;
    int ch;

    while ((ch = getc(evtdev)) != EOF) {
      *p ++ = ch;
      assert(p - buf < sizeof(buf));
      if (ch == '\n') {
        break;
      }
    }
    *p = '\0';

    if (p == buf) {
      continue;
    }

    if (buf[0] == 'k') {
      char keyname[32];
      event->type = buf[1] == 'd' ? NDL_EVENT_KEYDOWN : NDL_EVENT_KEYUP;
      event->data = -1;
      sscanf(buf + 3, "%s", keyname);
      for (int i = 0; i < numkeys; i ++) {
        if (strcmp(keys[i], keyname) == 0) {
          event->data = i;
          break;
        }
      }
      assert(event->data >= 1 && event->data < numkeys);
      return 0;
    }
    if (buf[0] == 't') {
      uint32_t tsc = 0;
      assert(sscanf(buf + 2, "%u", &tsc) == 1);
      event->type = NDL_EVENT_TIMER;
      event->data = (int32_t)tsc;
      return 0;
    }
  }

  assert(0);
  return -1;
}

static void get_display_info() {
  FILE *dispinfo = fopen("/proc/dispinfo", "r");
  assert(dispinfo);
  screen_w = screen_h = 0;
  char buf[128], key[128], value[128], *delim;
  while (fgets(buf, 128, dispinfo)) {
    *(delim = strchr(buf, ':')) = '\0';
    sscanf(buf, "%s", key);
    sscanf(delim + 1, "%s", value);
    if (strcmp(key, "WIDTH") == 0) sscanf(value, "%d", &screen_w);
    if (strcmp(key, "HEIGHT") == 0) sscanf(value, "%d", &screen_h);
  }
  fclose(dispinfo);
  assert(screen_w > 0 && screen_h > 0);
}
