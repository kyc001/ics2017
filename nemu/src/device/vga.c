// vga.c - VGA 显示设备模拟（PA3 设备驱动）
// 功能：模拟 VGA 显卡，提供图形显示接口
// 使用 SDL2 库创建窗口，客户程序通过显存（VMEM）写入像素

#include "common.h"

#ifdef HAS_IOE

#include "device/mmio.h"      // MMIO 接口
#include "device/port-io.h"   // 端口 I/O 接口
#include <SDL2/SDL.h>         // SDL 图形库
#include <poll.h>             // poll() 系统调用
#include <signal.h>           // kill() 函数
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>         // waitpid() 函数
#include <unistd.h>           // fork(), pipe()

#define VMEM 0x40000          // 显存起始地址：0x40000（256KB）
#define SCREEN_PORT 0x100     // 屏幕尺寸端口（非标准）
#define SCREEN_H 300          // 屏幕高度：300 像素
#define SCREEN_W 400          // 屏幕宽度：400 像素

static SDL_Window *window;    // SDL 窗口
static SDL_Renderer *renderer;// SDL 渲染器
static SDL_Texture *texture;  // SDL 纹理（存放像素数据）

static uint32_t (*vmem) [SCREEN_W];
// 显存指针：指向 SCREEN_H 行 × SCREEN_W 列的像素数组
// 每个像素 4 字节（ARGB 格式）

static uint32_t *screensize_port_base;
// 屏幕尺寸端口的基址

static bool screen_dirty = true;
// 屏幕脏标志：true = 显存被修改，需要刷新屏幕

typedef struct {
  int ret;                    // SDL_VideoInit 返回值
  char err[128];              // 错误信息
} SdlVideoProbeResult;

enum {
  SDL_VIDEO_PROBE_OK = 0,     // 探测成功
  SDL_VIDEO_PROBE_FAIL = 1,   // 探测失败
  SDL_VIDEO_PROBE_TIMEOUT = 2,// 探测超时
};

// host_display_requested: 是否请求使用宿主机显示
static bool host_display_requested() {
  const char *use_host = getenv("NEMU_USE_HOST_DISPLAY");
  return use_host != NULL && strcmp(use_host, "1") == 0;
}

// explicit_video_backend_selected: 是否显式选择了视频后端
static bool explicit_video_backend_selected() {
  const char *video = getenv("SDL_VIDEODRIVER");
  return video != NULL && video[0] != '\0';
}

// has_display_env: 是否设置了 DISPLAY 环境变量
static bool has_display_env() {
  const char *display = getenv("DISPLAY");
  return display != NULL && display[0] != '\0';
}

// should_try_host_display: 是否应该尝试宿主机显示
static bool should_try_host_display() {
  if (explicit_video_backend_selected()) {
    return false;
  }
  return host_display_requested() || has_display_env();
}

// enable_dummy_backend: 启用虚拟后端（无头模式）
// 参数：enable_terminal_keys = 是否启用终端键盘
//       reason = 原因（打印给用户）
static void enable_dummy_backend(bool enable_terminal_keys, const char *reason) {
  if (reason != NULL) {
    fprintf(stderr, "%s\n", reason);
  }
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  // dummy 后端：SDL 不创建真实窗口（无头模式）
  if (enable_terminal_keys) {
    setenv("NEMU_TERMINAL_KEYS_ACTIVE", "1", 1);
  }
}

// vga_vmem_io_handler: 显存 MMIO 处理函数
// 功能：当客户程序写显存时，标记屏幕需要刷新
static void vga_vmem_io_handler(paddr_t addr, int len, bool is_write) {
  (void)addr;
  (void)len;
  if (is_write) {                     // 如果是写操作
    screen_dirty = true;              // 标记屏幕脏
  }
}

// probe_host_video_backend: 探测宿主机视频后端
// 功能：fork 子进程尝试初始化 SDL 视频，避免阻塞主进程
// 参数：result = 探测结果，timeout_ms = 超时时间（毫秒）
// 返回：SDL_VIDEO_PROBE_OK/FAIL/TIMEOUT
static int probe_host_video_backend(SdlVideoProbeResult *result, int timeout_ms) {
  int pipefd[2];
  int ret = pipe(pipefd);             // 创建管道（用于父子进程通信）
  Assert(ret == 0, "pipe() failed");

  pid_t pid = fork();                 // fork 子进程
  Assert(pid >= 0, "fork() failed");

  if (pid == 0) {                     // 子进程
    close(pipefd[0]);                 // 关闭读端
    SdlVideoProbeResult probe = { .ret = SDL_VideoInit(NULL) };
    // 尝试初始化 SDL 视频
    if (probe.ret == 0) {
      SDL_VideoQuit();                // 成功则退出
    } else {
      snprintf(probe.err, sizeof(probe.err), "%s", SDL_GetError());
    }
    ssize_t nwritten = write(pipefd[1], &probe, sizeof(probe));
    // 把结果写入管道
    (void)nwritten;
    close(pipefd[1]);
    _exit(probe.ret == 0 ? 0 : 1);    // 子进程退出
  }

  // 父进程
  close(pipefd[1]);                   // 关闭写端
  struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
  ret = poll(&pfd, 1, timeout_ms);    // 等待子进程写入（带超时）
  if (ret == 0) {                     // 超时
    kill(pid, SIGKILL);               // 杀死子进程
    waitpid(pid, NULL, 0);
    close(pipefd[0]);
    memset(result, 0, sizeof(*result));
    return SDL_VIDEO_PROBE_TIMEOUT;
  }

  Assert(ret > 0, "poll() failed while probing SDL video backend");
  ssize_t nread = read(pipefd[0], result, sizeof(*result));
  // 读取子进程的结果
  close(pipefd[0]);
  waitpid(pid, NULL, 0);
  Assert(nread == sizeof(*result), "short read while probing SDL video backend");
  return result->ret == 0 ? SDL_VIDEO_PROBE_OK : SDL_VIDEO_PROBE_FAIL;
}

// update_screen: 更新屏幕显示
// 功能：把显存内容复制到 SDL 纹理并渲染
// 调用时机：device_update() 中（每 20ms 一次）
void update_screen() {
  if (!screen_dirty) {                // 如果屏幕没有变化
    return;                           // 不刷新（节省 CPU）
  }
  SDL_UpdateTexture(texture, NULL, vmem, SCREEN_W * sizeof(vmem[0][0]));
  // 把显存内容复制到纹理
  SDL_RenderClear(renderer);          // 清空渲染器
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  // 把纹理复制到渲染器
  SDL_RenderPresent(renderer);        // 呈现到窗口
  screen_dirty = false;               // 清除脏标志
}

// init_vga: 初始化 VGA 显示
// 功能：创建 SDL 窗口，注册显存 MMIO
void init_vga() {
  // 尝试探测宿主机显示
  if (should_try_host_display()) {
    SdlVideoProbeResult probe;
    int status = probe_host_video_backend(&probe, 2000);
    // 超时 2 秒
    if (status == SDL_VIDEO_PROBE_TIMEOUT) {
      if (getenv("NEMU_STRICT_HOST_DISPLAY")) {
        Assert(0, "SDL host video init timed out. DISPLAY=%s. This shell likely has no usable GUI session for NEMU.",
            getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
      }
      char msg[256];
      snprintf(msg, sizeof(msg),
          "warning: SDL host video init timed out on DISPLAY=%s, falling back to SDL_VIDEODRIVER=dummy.",
          getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
      enable_dummy_backend(true, msg);
      fprintf(stderr, "warning: enabling terminal keyboard fallback for headless interactive programs.\n");
    }
    else if (status != SDL_VIDEO_PROBE_OK) {
      if (getenv("NEMU_STRICT_HOST_DISPLAY")) {
        Assert(0, "SDL host video init failed: %s", probe.err);
      }
      char msg[256];
      snprintf(msg, sizeof(msg),
          "warning: SDL host video init failed (%s), falling back to SDL_VIDEODRIVER=dummy.",
          probe.err);
      enable_dummy_backend(true, msg);
      fprintf(stderr, "warning: enabling terminal keyboard fallback for headless interactive programs.\n");
    }
  } else if (!explicit_video_backend_selected()) {
    enable_dummy_backend(true, "note: no DISPLAY detected, using SDL_VIDEODRIVER=dummy.");
  }

  int ret = SDL_Init(SDL_INIT_VIDEO);
  Assert(ret == 0, "SDL_Init failed: %s", SDL_GetError());

  ret = SDL_CreateWindowAndRenderer(SCREEN_W * 2, SCREEN_H * 2, 0, &window, &renderer);
  // 创建窗口和渲染器，窗口大小 = 屏幕大小 × 2（放大显示）
  Assert(ret == 0, "SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
  SDL_SetWindowTitle(window, "NEMU");
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);
  // 创建纹理：ARGB8888 格式（每像素 4 字节）
  Assert(texture != NULL, "SDL_CreateTexture failed: %s", SDL_GetError());
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

  screensize_port_base = add_pio_map(SCREEN_PORT, 4, NULL);
  // 注册屏幕尺寸端口
  *screensize_port_base = (SCREEN_W << 16) | SCREEN_H;
  // 端口值 = (宽度 << 16) | 高度，例如 0x01900258 = 400×600
  vmem = add_mmio_map(VMEM, 0x80000, vga_vmem_io_handler);
  // 注册显存 MMIO：起始地址 0x40000，大小 512KB
}
#endif	/* HAS_IOE */

// ──────────────────────────────────────────────────────────────────────────────
// 客户程序使用显存示例（画一个红色像素）：
//
// #define VMEM 0x40000
// #define SCREEN_W 400
//
// void draw_pixel(int x, int y, uint32_t color) {
//   uint32_t *fb = (uint32_t *)VMEM;  // 显存起始地址
//   fb[y * SCREEN_W + x] = color;     // 写入像素
// }
//
// void main() {
//   draw_pixel(100, 100, 0xFF0000);  // 在 (100,100) 画红色像素
// }
//
// 颜色格式（ARGB8888）：
//   0xAARRGGBB
//   AA = Alpha（透明度，通常 0xFF）
//   RR = Red（红色分量）
//   GG = Green（绿色分量）
//   BB = Blue（蓝色分量）
// ──────────────────────────────────────────────────────────────────────────────
