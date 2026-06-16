# PA 答辩演示运行指令

本文按答辩要求整理，可以在仓库根目录 `/workspace/ics2017` 直接复制运行。


## 0. 准备环境变量

每次新开终端，先执行：

```bash
cd /workspace/ics2017
export NEMU_HOME="$PWD/nemu"
export AM_HOME="$PWD/nexus-am"
export NAVY_HOME="$PWD/navy-apps"
```

如果后续要运行 `typing` 或仙剑奇侠传，需要能弹出 SDL 窗口。答辩现场如果是 WSL/SSH/X11 环境，先确认：

```bash
echo "$DISPLAY"
```

能看到类似 `:0`、`:1`、`localhost:10.0` 的输出即可继续。

## 1. 使用 runall.sh 通过 cputest 测试

```bash
cd /workspace/ics2017
export NEMU_HOME="$PWD/nemu"
export AM_HOME="$PWD/nexus-am"
export NAVY_HOME="$PWD/navy-apps"

cd nemu
bash runall.sh
```

预期现象：先输出 `NEMU compile OK`、`testcases compile OK`，然后每个 cputest 用例显示绿色 `PASS!`。

## 2. 运行 nexus-am/apps 下的 hello 和 typing

### 2.1 hello

```bash
cd /workspace/ics2017
export NEMU_HOME="$PWD/nemu"
export AM_HOME="$PWD/nexus-am"
export NAVY_HOME="$PWD/navy-apps"

cd nexus-am/apps/hello
make ARCH=x86-nemu run
```

预期现象：终端输出多行 `Hello World!`，最后出现 `nemu: HIT GOOD TRAP`。

### 2.2 typing

```bash
cd /workspace/ics2017
export NEMU_HOME="$PWD/nemu"
export AM_HOME="$PWD/nexus-am"
export NAVY_HOME="$PWD/navy-apps"

cd nexus-am/apps/typing
NEMU_USE_HOST_DISPLAY=1 make ARCH=x86-nemu run
```

预期现象：弹出图形窗口，能看到打字小游戏画面。演示结束后可在终端按 `Ctrl-C` 退出。

## 3. 切换到 pa3 分支并启动仙剑奇侠传

先切换分支，再更新 Navy-apps 文件系统镜像，最后让 Nanos-lite 加载 `/bin/pal`：

```bash
cd /workspace/ics2017

export NEMU_HOME="$PWD/nemu"
export AM_HOME="$PWD/nexus-am"
export NAVY_HOME="$PWD/navy-apps"

make -C nanos-lite ARCH=x86-nemu update
NEMU_USE_HOST_DISPLAY=1 make -C nanos-lite ARCH=x86-nemu run DEFAULT_PROGRAM=/bin/pal
```

预期现象：弹出仙剑奇侠传窗口，可以进入“新的故事”或读取存档。PA3 阶段按讲义要求先不要触发战斗。

演示结束后可在终端按 `Ctrl-C` 退出。

## 4. 可选：PA4/PA5 演示仙剑战斗，同时终端输出 hello

使用你最终完成 PA4/PA5 的分支。本仓库当前 `master` 和 `pa5` 指向同一份最终实现，下面默认使用 `pa5`：

```bash
cd /workspace/ics2017

export NEMU_HOME="$PWD/nemu"
export AM_HOME="$PWD/nexus-am"
export NAVY_HOME="$PWD/navy-apps"

make -C nanos-lite ARCH=x86-nemu update
NEMU_USE_HOST_DISPLAY=1 
make -C nanos-lite ARCH=x86-nemu run DEFAULT_PROGRAM=/bin/pal
```

预期现象：

1. 仙剑奇侠传正常启动。
2. 读取可触发战斗的存档或在地图中遇敌，进入战斗画面。
3. 终端同时持续输出 `hello` / `Hello` 相关内容，说明仙剑和 hello 程序在分时运行。

演示结束后可在终端按 `Ctrl-C` 退出。

## 5. 演示完切回最终分支



等角色行动轮到时，左下会出现 4 个图标。
用 方向键 选图标：
上：普通攻击
左：法术/技能
右：合体技
下：杂项
按 Enter/Space 确认。
进技能列表后，用方向键选技能，Enter 确认，再选目标确认。
注意：战斗里 WASD 不是方向键。当前映射是快捷键：

D 防御
E 使用物品
W 投掷物品
Q 逃跑
F 自动选法术/强攻
R 重复上次行动
Esc 返回/取消


---

## 1. ramdisk 存储了什么？

* **本质**：NEMU 模拟的一块连续内存（"伪磁盘"），通过 `nanos-lite` 中的 `.incbin` 直接**硬编码嵌入内核**。
* **内容**：按顺序拼接的 **Navy 用户程序可执行文件（ELF）**（如 `/bin/pal`, `/bin/nterm`）。
* **接口**：极其朴素，`ramdisk_read/write` 仅基于 `ramdisk_start` 基址进行 `memcpy`。

---

## 2. `make update` 的实现与工作流

在 `nanos-lite/` 下执行，实现**数据与元数据的同步更新**：

1. **打包镜像**：遍历 Navy 编译出的可执行文件，依次追加到 `build/ramdisk.img`。
2. **生成配置**：自动生成 `src/files.h`，将每个文件的 `name`、`size` 以及在镜像中的 `disk_offset` 写入 `Finfo` 结构体数组。

> **⚠️ 经典大坑**：修改 Navy 程序后若不执行 `make update`，会导致 `files.h` 中的元数据过期，加载时发生程序**错位或截断**。

---

## 3. `files.h` 的作用与文件系统分发

* **元数据表**：作为 `file_table[]` 的核心初始化数据，向 Nanos-lite 文件系统（`fs.c`）宣告所有静态文件的**边界与位置**。
* **分发机制**：
* **普通文件**：`fs_open` 查表获取 `disk_offset` ──> `fs_read/write` 路由至 `ramdisk_read/write`。
* **设备文件**：在表头或特定位置手动注册特殊文件（如 `/dev/fb`, `/dev/events`），通过特异化的虚函数指针（`read/write` 钩子）分发至硬件驱动（如 `fb_write`）。



---

## 4. x86 寄存器结构与 NEMU 实现 (PA1)

### 数量与位宽

* **通用寄存器 (GPR)**：8 个 32 位通用寄存器（顺序严格为 `eax, ecx, edx, ebx, esp, ebp, esi, edi`），支持 16 位和 8 位切片访问。
* **控制与状态**：`eip` (PC)、`eflags`，以及后续的 `cr0/cr3`、`idtr`。

### 嵌套共用体 (Union) 实现方案

利用 **Union 共享内存**的特性，完美模拟 x86 寄存器的别名机制：

```c
typedef struct {
  union {
    union {
      uint32_t _32; uint16_t _16; uint8_t _8[2];
    } gpr[8]; // 视图一：数组形式，便于译码期通过 index 索引
    struct {
      rtlreg_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    };        // 视图二：结构体形式，便于代码中按名字直观访问
  };
  vaddr_t eip;
} CPU_state;

```
