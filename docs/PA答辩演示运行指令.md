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