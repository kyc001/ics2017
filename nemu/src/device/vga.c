#include "common.h"

#ifdef HAS_IOE

#include "device/mmio.h"
#include "device/port-io.h"
#include <SDL2/SDL.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define VMEM 0x40000
#define SCREEN_PORT 0x100 // Note that this is not the standard
#define SCREEN_H 300
#define SCREEN_W 400

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static uint32_t (*vmem) [SCREEN_W];
static uint32_t *screensize_port_base;

typedef struct {
  int ret;
  char err[128];
} SdlVideoProbeResult;

enum {
  SDL_VIDEO_PROBE_OK = 0,
  SDL_VIDEO_PROBE_FAIL = 1,
  SDL_VIDEO_PROBE_TIMEOUT = 2,
};

static bool host_display_requested() {
  const char *use_host = getenv("NEMU_USE_HOST_DISPLAY");
  return use_host != NULL && strcmp(use_host, "1") == 0;
}

static bool should_use_dummy_video_backend() {
  const char *video = getenv("SDL_VIDEODRIVER");
  return video == NULL && !host_display_requested();
}

static int probe_host_video_backend(SdlVideoProbeResult *result, int timeout_ms) {
  int pipefd[2];
  int ret = pipe(pipefd);
  Assert(ret == 0, "pipe() failed");

  pid_t pid = fork();
  Assert(pid >= 0, "fork() failed");

  if (pid == 0) {
    close(pipefd[0]);
    SdlVideoProbeResult probe = { .ret = SDL_VideoInit(NULL) };
    if (probe.ret == 0) {
      SDL_VideoQuit();
    } else {
      snprintf(probe.err, sizeof(probe.err), "%s", SDL_GetError());
    }
    ssize_t nwritten = write(pipefd[1], &probe, sizeof(probe));
    (void)nwritten;
    close(pipefd[1]);
    _exit(probe.ret == 0 ? 0 : 1);
  }

  close(pipefd[1]);
  struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
  ret = poll(&pfd, 1, timeout_ms);
  if (ret == 0) {
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    close(pipefd[0]);
    memset(result, 0, sizeof(*result));
    return SDL_VIDEO_PROBE_TIMEOUT;
  }

  Assert(ret > 0, "poll() failed while probing SDL video backend");
  ssize_t nread = read(pipefd[0], result, sizeof(*result));
  close(pipefd[0]);
  waitpid(pid, NULL, 0);
  Assert(nread == sizeof(*result), "short read while probing SDL video backend");
  return result->ret == 0 ? SDL_VIDEO_PROBE_OK : SDL_VIDEO_PROBE_FAIL;
}

void update_screen() {
  SDL_UpdateTexture(texture, NULL, vmem, SCREEN_W * sizeof(vmem[0][0]));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

void init_vga() {
  if (should_use_dummy_video_backend()) {
    setenv("SDL_VIDEODRIVER", "dummy", 0);
  }
  else if (host_display_requested() && getenv("SDL_VIDEODRIVER") == NULL) {
    SdlVideoProbeResult probe;
    int status = probe_host_video_backend(&probe, 2000);
    if (status == SDL_VIDEO_PROBE_TIMEOUT) {
      Assert(0, "SDL host video init timed out. DISPLAY=%s. This shell likely has no usable GUI session for NEMU.",
          getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
    }
    Assert(status == SDL_VIDEO_PROBE_OK, "SDL host video init failed: %s", probe.err);
  }

  int ret = SDL_Init(SDL_INIT_VIDEO);
  Assert(ret == 0, "SDL_Init failed: %s", SDL_GetError());

  ret = SDL_CreateWindowAndRenderer(SCREEN_W * 2, SCREEN_H * 2, 0, &window, &renderer);
  Assert(ret == 0, "SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
  SDL_SetWindowTitle(window, "NEMU");
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);
  Assert(texture != NULL, "SDL_CreateTexture failed: %s", SDL_GetError());
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

  screensize_port_base = add_pio_map(SCREEN_PORT, 4, NULL);
  *screensize_port_base = (SCREEN_W << 16) | SCREEN_H;
  vmem = add_mmio_map(VMEM, 0x80000, NULL);
}
#endif	/* HAS_IOE */
