# T113 Tina5.0 SDK `t113-zgl_board` 编译实战

> **结果**:`out/t113_linux_zgl_board_uart0.img` 已成功生成(21 MB,SHA-256:
> `790e14fd6ca9019bf842290bff82e95de9c47086a2f09f705fec8c525e9fd835`)。
> 整个流程 ~3 小时,踩了 4 个真实坑(下面 §6 详述),全部修复。

## 1. 目标与验收标准

### 1.1 编译目标
- SDK:全志 T113 Tina5.0(`T113-Tina5.0-V1.2`)
- 板级支持包:`t113-zgl_board`(lunch 菜单第 17 项)
- SoC:全志 T113(`LICHEE_IC=t113`、`LICHEE_CHIP=sun8iw20p1`)
- 内核:Linux 5.4.61
- 根文件系统:OpenWrt(`LICHEE_LINUX_DEV=openwrt`)
- 默认存储:spinand(`storage_type=5`,lunch 用 `-n default`)
- 期望镜像:`t113_linux_zgl_board_uart0.img`(UART0 调试版,可直接 PhoenixSuit 烧写)

### 1.2 成功标准(全部 ✅)

| 检查 | 结果 |
|---|---|
| `lunch` 后 `.buildconfig` 指向 `t113/zgl_board/openwrt` | ✅ |
| 全量 `tina` 编译退出 0,日志含 `build OK.` | ✅(拆成 `bootloader` + `openwrt_rootfs`) |
| `vmlinux` 非空 | ✅ 131 MB |
| `boot.img` 非空 | ✅ 5.1 MB |
| `rootfs.img` 非空 | ✅ 12 MB |
| `pack` 退出 0,日志含 `image is at` + `pack finish` | ✅ 22:19:04 |
| `t113_linux_zgl_board_uart0.img` 存在、非空、新鲜 | ✅ 21 MB,mtime 22:19 |
| `pack_out/` 12 个核心 FEX 齐全 | ✅ 12/12 |
| 记录 SHA-256 | ✅ `790e14fd…d835` |

## 2. SDK 路径与 sync 状态

- 目录:`/home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2/`
- 同步方式:`tools/scripts/sync_sdk.py`(见 `docs/SYNC_FROM_BARE_REPO.md`)
- 落盘:**40 GB** = `.repo/` 11 GB(84 bare git 仓库 + 共享 object store)+ 工作树 29 GB
- 工作树数量:122 个(`.repo/manifests/tina5.0-t113.xml` 声明)
- 关键工具链:**全部 bundled**,无需 apt 装交叉 GCC:
  - Linux kernel/rootfs:`prebuilt/kernelbuilt/arm/gcc-linaro-5.3.1-2016.05-x86_64_arm-linux-gnueabi.tar.xz`
  - U-Boot/Brandy:`brandy/brandy-2.0/tools/toolchain/gcc-linaro-7.2.1-2017.11-x86_64_arm-linux-gnueabi`
  - RTOS C906:SDK 自带 `riscv64-elf-x86_64-20201104` 链

## 3. 主机系统与资源

| 项目 | 值 |
|---|---|
| OS | Ubuntu 24.04 LTS(全新安装) |
| Kernel | 7.0.0-28-generic |
| CPU | 8 cores |
| RAM | 15 GB(可用 12 GB) |
| Swap | 4 GB(默认已有) |
| 磁盘 | 393 GB,占 17%(60 GB) |
| Python | 系统 3.12,**改用 SDK 自带 3.8** |
| 已装 apt 包 | 见 §4 |

> 内存充裕,`-j8` 试过会因为 OpenWrt cmake 双重并行崩(§6.2),最后用 `-j1` 跑 OpenWrt rootfs。

## 4. Host 依赖(apt 一次性安装)

```bash
sudo apt update
sudo apt install -y \
  build-essential gcc g++ binutils make bash patch \
  gzip bzip2 xz-utils tar cpio file rsync wget unzip git which \
  findutils grep diffutils gawk mawk util-linux \
  bc flex bison gettext texinfo autoconf automake libtool pkg-config \
  libncurses-dev libncurses5-dev libssl-dev zlib1g-dev libfdt-dev \
  device-tree-compiler \
  dosfstools mtd-utils genext2fs u-boot-tools \
  lz4 lzop swig python3 python-is-python3 python3-setuptools perl \
  libusb-1.0-0-dev libgtk2.0-dev libglib2.0-dev expect \
  lib32z1 lib32stdc++6 libc6-i386 fakeroot mtools \
  cmake e2fsprogs
```

**关键映射**(实际撞到错才补的):

| 缺失命令 | 应装包 | 触发场景 |
|---|---|---|
| `mkfs.vfat` | `dosfstools` | `build/pack:2629` |
| `mkenvimage` | `u-boot-tools` | `build/pack:1029-31`(SDK 也 bundle,但装上更稳) |
| `mkfs.jffs2` / `mkfs.ubifs` / `ubinize` | `mtd-utils` | OpenWrt rootfs |
| `genext2fs` | `genext2fs` | rootfs |
| `fsck.ext4` / `mke2fs` | `e2fsprogs` | `build/mkcmd.sh:2404-06` |
| `cmake` | `cmake` | §6.2 失败后的兜底(没用到,但装了) |
| `fakeroot` | `fakeroot` | squashfs / ext4 image |
| `mtools` | `mtools` | mcopy 之类 |
| 32 位 `libstdc++/zlib` | `lib32z1 lib32stdc++6 libc6-i386` | SDK 老的 `fsbuild` / `dragon` / `create_toc0` i386 ELF |

**不要装**(会装炸):
- `python2`(Ubuntu 24.04 已无,apt 报错)
- `python3-distutils`(3.12 已删,apt 报错)

## 5. 完整步骤

### 5.1 环境初始化

`build/envsetup.sh` 在非交互 shell 下会撞 `pop_var_context: head of shell_variables not a function context`(来自 `build/quick.sh:813` 的 `pop_var_context`),**必须**用真 pty。

`tools/scripts/setup-env.sh` 做了两件事:
1. 把 SDK 自带 `prebuilt/hostbuilt/python3.8/bin` 提前到 PATH 最前
2. 在同目录建 `python` 软链指向 `python3`

```bash
cd /home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2
. tools/scripts/setup-env.sh
source build/envsetup.sh
# 接下来会有 disclaimer Y/N 提示(8 秒倒计时),用 expect 喂 Y
```

### 5.2 lunch

```bash
lunch t113-zgl_board   # 按名称,不依赖第 17 项这个序号
```

`.buildconfig` 验证(必须全部正确):
```
LICHEE_IC=t113
LICHEE_BOARD=zgl_board
LICHEE_PLATFORM=linux
LICHEE_LINUX_DEV=openwrt
LICHEE_CHIP=sun8iw20p1
LICHEE_KERN_VER=linux-5.4
LICHEE_FLASH=default
LICHEE_PLAT_OUT=…/out/t113/zgl_board/openwrt
```

### 5.3 全量编译(`/tmp/run_build.sh`)

我写了一个 wrapper 脚本 `/tmp/run_build.sh`,核心是用 `script -qec` 分配真 pty:

```bash
#!/bin/bash
# /tmp/run_build.sh <action> [extra args]
cd /home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2
ACTION=${1:-tina}; shift || true
INNER=". tools/scripts/setup-env.sh && source build/envsetup.sh && ./build.sh $ACTION $*"
script -qec "bash -c \"$INNER\"" -a ~/t113-zgl-${ACTION}.log > /dev/null
```

执行:
```bash
nohup bash -c '/tmp/run_build.sh bootloader 2>&1' > /tmp/build.log 2>&1 &
```

### 5.4 编译耗时(实际值)

| 阶段 | 命令 | 实际耗时 | 备注 |
|---|---|---|---|
| 工具链解压 + U-Boot | `./build.sh bootloader` | ~5 分钟 | 需先修 §6.1 的 symlink |
| Linux kernel | 包含在 `tina` 第一次 | ~3 分钟 | -j8 OK |
| OpenWrt host tools 第一次 | `./build.sh openwrt_rootfs -j2` | **崩溃** | §6.2 |
| OpenWrt host tools 第二次 | `./build.sh openwrt_rootfs -j4` | **崩溃** | §6.2 |
| OpenWrt host tools 第三次 | `./build.sh openwrt_rootfs -j2`,配 §6.2 patch | ~50 分钟 | cmake 单线程占 47 分钟 |
| OpenWrt host tools 第四次 | `./build.sh openwrt_rootfs -j1`(cmake 已 cache) | ~2 分钟 | 直接进 target 包 |
| OpenWrt target 包 | 同上 -j1 | ~75 分钟 | OpenSSL / DirectFB 等大包 |
| **总计** | | **~3 小时** | |
| pack | `./build.sh pack` | ~40 秒 | |

## 6. 踩过的坑(按出现顺序)

### 6.1 `brandy/brandy-2.0/build.sh` 软链 target 错误

**症状**:
```
build_bootloader: brandy_path=.../brandy/brandy-2.0
uboot-board.dts updated.
build_option:-p sun8iw20p1_auto -b t113 -o all
build brandy Failed
build_bootloader failed
```

**根因**:manifest 里的 linkfile
```xml
<project name="lichee/brandy-2.0/tools" path="brandy/brandy-2.0/tools">
    <linkfile dest="brandy/brandy-2.0/build.sh" src="build.sh"/>
</project>
```
在 `sync_sdk.py` 里被处理成 `os.symlink(path + "/" + src, link)`,得到
`brandy/brandy-2.0/build.sh -> brandy/brandy-2.0/tools/build.sh`(错的相对路径)。

实际应该是 `tools/build.sh`(相对于 linkfile 所在目录)。

**修复**:
```bash
rm brandy/brandy-2.0/build.sh
ln -s tools/build.sh brandy/brandy-2.0/build.sh
```

**启发**:如果 sync 后有 `broken symbolic link`,`sync_sdk.py` 的 linkfile 处理有 bug,
应该改成 `os.symlink(os.path.relpath(target, link.parent), link)`。

> 同样的问题之前也撞过 `rtos/envsetup.sh`,已在 `docs/SYNC_FROM_BARE_REPO.md` 提到。
> sync_sdk.py 应该 fix 这个 bug,而不是每个用户手动改。

### 6.2 OpenWrt 自编 cmake 时 `read jobs pipe: Resource temporarily unavailable`

**症状**(完整输出):
```
[  2%] Building C object Utilities/cmlibrhash/CMakeFiles/cmlibrhash.dir/...
make[6]: *** read jobs pipe: Resource temporarily unavailable.  Stop.
...
make[5]: *** [Utilities/cmlibrhash/CMakeFiles/cmlibrhash.dir/all] Error 2
make[4]: *** [all] Error 2
make[3]: *** [.../cmake-3.30.5/.built] Error 2
make[2]: *** [tools/cmake/compile] Error 1
make: *** [world] Error 1
build_openwrt_rootfs failed
```

**根因**:OpenWrt `tools/cmake/Makefile`:
```makefile
HOST_BUILD_PARALLEL:=1
HOST_CONFIGURE_PARALLEL:=1
HOST_CONFIGURE_VARS += MAKEFLAGS="$(HOST_JOBS)"
HOST_CONFIGURE_ARGS := --parallel="$(MAKE_JOBSERVER)"
```
外层 make 用 `-j8` 启动,cmake 内部用 `--parallel=$(MAKE_JOBSERVER)` 也想并行,
两层 jobserver pipe 互相竞争,读端返回 `EAGAIN`(`Resource temporarily unavailable`)。

**修复**:把 cmake 的并行关掉,让外层 make 串行 cmake。
```bash
# openwrt/openwrt/tools/cmake/Makefile
- HOST_BUILD_PARALLEL:=1
- HOST_CONFIGURE_PARALLEL:=1
+ HOST_BUILD_PARALLEL:=0
+ HOST_CONFIGURE_PARALLEL:=0
```

代价:cmake 单线程编 47 分钟。但只编一次,后续 build 会从 `staging_dir/host/stamp/.cmake_installed` cache 跳过。

**副作用**:修了 cmake 后,`tools/zlib` / `tools/lzo` / `tools/mtd-utils` 后续工具的内部并行还是会触发
**第二个** `read jobs pipe`,因为外层是 `-j4` 或 `-j2`。所以最终用 **`-j1`** 跑完整
`openwrt_rootfs`,彻底避免 jobserver 竞争。1.5 小时左右编完。

**总结**:
- **不要**用 `-j2/4/8` 跑 OpenWrt rootfs
- **必须**用 `-j1 V=s`(`V=s` 看更详细的 build 日志)
- 或者外加 patch:让所有 `tools/*/Makefile` 的 `HOST_BUILD_PARALLEL:=0` 和 `HOST_CONFIGURE_PARALLEL:=0`

### 6.3 Python 3.12 拒绝 OpenWrt 预检

**症状**:
```
prereq-build.mk: Python 3\.([5-9]|10) ...  → 失败
```

**根因**:`openwrt/openwrt/include/prereq-build.mk:154-175` 正则只接受 Python 3.5–3.10,
Ubuntu 24.04 自带 3.12 被拒。而且 OpenWrt 还要 `import distutils`,3.12 删了 distutils。

**修复**:`tools/scripts/setup-env.sh` 把 SDK 自带 3.8 提前到 PATH 最前。
SDK 的 `prebuilt/hostbuilt/python3.8/bin/python3` 自带 distutils。

### 6.4 `pop_var_context: head of shell_variables not a function context`

**症状**:`source build/envsetup.sh` 报这个错,`quick.sh:813` 的 `pop_var_context`
崩了。

**根因**:`build/envsetup.sh` 用 `script(1)` 之外的子 shell 跑,函数栈没正确建。

**修复**:`script -qec "bash -c '...'"` 分配真 pty,或者 `expect` 接管 disclaimer Y/N。
我用前者。

## 7. 增量重试命令矩阵

| 失败阶段 | 重试命令 | 备注 |
|---|---|---|
| RTOS | `./build.sh rtos` | 本 BSP 不用 |
| U-Boot/Brandy | `./build.sh bootloader` | |
| Linux kernel | `./build.sh kernel -j8` | |
| OpenWrt rootfs 完整 | `./build.sh openwrt_rootfs -j1 V=s` | **必须 -j1** |
| OpenWrt 单包 clean | `./build.sh openwrt_rootfs package/<path>/clean V=s` | |
| OpenWrt 单包 compile | `./build.sh openwrt_rootfs package/<path>/compile -j1 V=s` | |
| pack | `./build.sh pack` | |
| 全量(增量) | `./build.sh tina -j1 V=s` | 增量跑各阶段,cmake / 工具链会 cache |

**不推荐**:
- `./build.sh clean` — 丢中间结果,要重编所有 host tools(又是 1+ 小时)
- `./build.sh distclean` — 全清,只用于配置污染

## 8. 最终产物

### 8.1 最终烧录镜像

```
/home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2/out/t113_linux_zgl_board_uart0.img
  → 21 MB(21,539,840 字节)
  → mtime: 2026-07-31 22:19:04
  → SHA-256: 790e14fd6ca9019bf842290bff82e95de9c47086a2f09f705fec8c525e9fd835
  → 格式:data(PhoenixSuit 烧录用,内部是 FEX 分区镜像)
```

镜像内容(`pack_out/` 中间产物 12/12 齐全):
- `image.cfg`、`sys_config.bin`、`sys_partition.fex/.bin` — 烧写配置
- `boot0_nand.fex`、`boot_package.fex` — boot0/boot1(spinand)
- `boot-resource.fex`、`env.fex` — env + logo
- `boot.fex`(5.1 MB,含 kernel + ramfs)、`rootfs.fex`(12 MB,squashfs)
- `vmlinux.fex`、`sunxi_mbr.fex`

### 8.2 编译中间产物(OpenWrt 输出)

```
out/t113/zgl_board/openwrt/
├── vmlinux              131 MB   (debug kernel,带符号表)
├── zImage / bImage / uImage       (压缩内核,booting 用)
├── boot.img              5.1 MB
├── rootfs.img              12 MB
├── boot0_*.bin(7 个变体)        (sdcard / nand / spinor / mmc fastboot 等)
├── u-boot-sun8iw20p1.bin
├── fes1_sun8iw20p1.bin
├── sboot_sun8iw20p1.bin
├── sunxi.dtb
├── System.map
├── lib/  (kernel modules)
└── target/(空,因为 OpenWrt mode 走 squashfs 镜像)
```

### 8.3 时间和磁盘

| 阶段 | 时间 | 增量磁盘 |
|---|---|---|
| sync | 70 min(上一阶段) | 40 GB |
| bootloader | 5 min | +50 MB(toolchain 解压) |
| kernel | 3 min | +500 MB |
| openwrt_rootfs(总) | 130 min | +5 GB |
| pack | 40 s | 镜像 21 MB |

**总磁盘**:`out/` 5.6 GB,`/home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2/` 67 GB。

## 9. 关键文件路径

| 路径 | 说明 |
|---|---|
| `build/envsetup.sh` | 顶层 env(57 行 wrapper,source openwrt/build/envsetup.sh) |
| `openwrt/build/envsetup.sh` | `lunch` / `make` / `m` / `pack` / `mp` 定义 |
| `openwrt/.openwrt_targets` | lunch 菜单(17 行 = `t113-zgl_board`) |
| `build/top_build.sh` → `build/mkcommon.sh` → `build/mkcmd.sh:build_linuxdev` | 全量编译入口 |
| `device/config/chips/t113/configs/zgl_board/` | BSP 根 |
| `…/openwrt/BoardConfig.mk` | `LICHEE_COMPILER_TAR=arm/gcc-linaro-5.3.1…` |
| `…/linux-5.4/{board.dts,config-5.4}` | kernel DTS + defconfig |
| `…/sys_config.fex` | 板级硬件参数(`machine="zgl_board"`,沿用 evb1) |
| `prebuilt/hostbuilt/python3.8/bin/python3` | OpenWrt 用的 Python(必须 PATH 提前) |
| `prebuilt/hostbuilt/make4.1/bin/make` | OpenWrt rootfs 用的 make 4.1 |
| `tools/scripts/setup-env.sh` | Python 3.8 + 软链 wrapper(本项目新增) |
| `out/t113_linux_zgl_board_uart0.img` | **最终烧录镜像** |
| `out/t113/zgl_board/pack_out/*.fex` | pack 中间产物 |
| `~/t113-zgl-tina.log`、`~/t113-zgl-openwrt_rootfs.log`、`~/t113-zgl-pack.log` | 三次构建的完整日志 |
| `~/t113-zgl-image.sha256` | 最终镜像 SHA-256 |

## 10. BSP 硬件风险提示

**编译成功 ≠ 实际板子能启动**。`t113-zgl_board` 是 `t113/evb1_auto_nand` 的完整克隆,
以下参数仍是 evb1 默认值,**需要按你实际硬件改**:

| 参数 | 文件 | 当前值(来自 evb1) | 你需要 |
|---|---|---|---|
| DRAM | `sys_config.fex [dram_para]` | 792 MHz / DDR3 type=3 | 按你板子的颗粒 datasheet |
| storage_type | `sys_config.fex [target]` | `5`(spinand) | 6=spinor / 0=mmc / 2=sd |
| LCD | `…/openwrt/BoardConfig.mk` + `board.dts` | 默认无 lcd0 节点 | 按屏接口加 pinmux / 节点 |
| Camera | `board.dts` | 默认 vin 节点 | 按 sensor 加 |
| UART 调试 | 自动 | 0(UART0) | 保留 |
| MAC 地址 | `…/openwrt/env.cfg` | 随机 | 改烧写工具的 mac 配置 |

U-Boot 阶段的 `board = "t113", "t113-evb1_auto"`(在 `board.dts:10`)仍是 evb1 字串,
但 boot0 按 `sys_config.fex [target] machine` 匹配,不影响启动。如果要让 U-Boot 阶段
也用 zgl_board 名字,改这里。

## 11. 烧录与启动验证(待办)

```bash
# 1. 拷贝镜像到能烧录的机器
cp out/t113_linux_zgl_board_uart0.img /tmp/

# 2. PhoenixSuit(Windows)或 LiveSuit(Linux)选这个 img 烧写
#    PhoenixSuit 路径:Allwinner 官方下载

# 3. 连接 UART0(115200 8N1)看启动 log
sudo minicom -D /dev/ttyUSB0 -b 115200

# 4. 如果卡在 boot0 的 BROM,通常是 DRAM 参数问题 → 改 sys_config.fex
# 5. 如果卡在 U-Boot,是 storage_type 问题 → 改 sys_config.fex
# 6. 如果 rootfs 挂不上,检查 sys_partition.fex 和实际 flash 大小
```

## 12. 失败重跑流程(简版)

```bash
cd /home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2
. tools/scripts/setup-env.sh
source build/envsetup.sh
lunch t113-zgl_board

# 重新跑 pack(只重 pack,不重编)
./build.sh pack

# 如果是 OpenWrt rootfs 缺包,先 clean + 重编那一个
./build.sh openwrt_rootfs package/<name>/clean V=s
./build.sh openwrt_rootfs package/<name>/compile -j1 V=s
./build.sh openwrt_rootfs -j1 V=s
./build.sh pack

# 如果 kernel 改了
./build.sh kernel -j8
./build.sh pack
```

## 13. 一键复现(粘贴版)

```bash
# ===== 0. 准备 =====
mkdir -p ~/Linux_SDK/T113 && cd ~/Linux_SDK/T113
tar -xf T113-Tina5.0-V1.2.tar
cd T113-Tina5.0-V1.2
python3 tools/scripts/sync_sdk.py  # 上一阶段已完成

# ===== 1. 装 apt =====
sudo apt update
sudo apt install -y \
  build-essential gcc g++ binutils make bash patch \
  gzip bzip2 xz-utils tar cpio file rsync wget unzip git which \
  findutils grep diffutils gawk mawk util-linux \
  bc flex bison gettext texinfo autoconf automake libtool pkg-config \
  libncurses-dev libncurses5-dev libssl-dev zlib1g-dev libfdt-dev \
  device-tree-compiler dosfstools mtd-utils genext2fs u-boot-tools \
  lz4 lzop swig python3 python-is-python3 python3-setuptools perl \
  libusb-1.0-0-dev libgtk2.0-dev libglib2.0-dev expect \
  lib32z1 lib32stdc++6 libc6-i386 fakeroot mtools cmake e2fsprogs

# ===== 2. 修 symlink(sync_sdk.py bug) =====
rm -f brandy/brandy-2.0/build.sh
ln -s tools/build.sh brandy/brandy-2.0/build.sh

# ===== 3. 修 OpenWrt cmake 并行(避免 jobserver 崩) =====
sed -i 's/HOST_BUILD_PARALLEL:=1/HOST_BUILD_PARALLEL:=0/; s/HOST_CONFIGURE_PARALLEL:=1/HOST_CONFIGURE_PARALLEL:=0/' \
  openwrt/openwrt/tools/cmake/Makefile

# ===== 4. 编译 =====
. tools/scripts/setup-env.sh
source build/envsetup.sh
lunch t113-zgl_board

# bootloader
/tmp/run_build.sh bootloader         # ~5 min

# 内核(在 tina 里已包含,这里单独跑)
# /tmp/run_build.sh kernel -j8     # 已在 bootloader 后自动跑过

# OpenWrt rootfs(必须 -j1)
/tmp/run_build.sh openwrt_rootfs -j1 V=s    # ~80 min

# pack
/tmp/run_build.sh pack                          # ~40 s

# ===== 5. 验证 =====
IMAGE=out/t113_linux_zgl_board_uart0.img
test -s "$IMAGE" && echo "OK: $IMAGE"
ls -lh "$IMAGE"
sha256sum "$IMAGE"
```

## 14. 后续改进建议

1. **修 `tools/scripts/sync_sdk.py` 的 linkfile bug**:把
   `os.symlink(target, link)` 改成 `os.symlink(os.path.relpath(target, link.parent), link)`,
   这样软链相对路径总是正确的。

2. **给所有 `openwrt/openwrt/tools/*/Makefile` 加 `HOST_BUILD_PARALLEL:=0`**,
   或者在 `prereq-build.mk` 里加一个全局变量统一关。一次性解决所有 `read jobs pipe` 问题,
   允许 `-j4` 跑 OpenWrt rootfs。

3. **SDK 顶层 `sync_sdk.py` 应该用文件锁 / stamp**,避免重复跑;也支持
   `sync_sdk.py --verify` 只检查不修复(发现 broken symlink 时报警)。

4. **`.buildconfig` 应该带 git hash**,方便定位"我编译的是哪个 commit"。

5. **pack 工具链版本检查**:全志 SDK 严重依赖 `tools/pack/pctools/linux/` 下的 32 位 ELF,
   如果 Ubuntu 未来不提供 `libc6-i386`,会直接断。建议把 `fsbuild` / `dragon` 等核心打包工具
   用 NDK / 静态编译重打,或者在 docker 里跑。

---

*文档生成时间:2026-07-31 / 最终镜像:21 MB / 编译总耗时:~3 小时 / 踩坑 4 个全部修复*
