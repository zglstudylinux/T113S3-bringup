# Docker 编译 T113 SDK 全流程实录(Ubuntu 22.04 容器,从零到镜像)

> 目的:解决"不同人 Ubuntu 版本不一样(20.04/22.04/24.04),编译 SDK 遇到不同报错"的问题。
> 本篇是 **2026-10-20 实测全记录**:在干净的 Ubuntu 22.04 容器里,从解压原厂 tar 包开始,
> 按教程适配、全量编译,产出 `t113_linux_zgl_board_uart0.img`,与宿主机直编结果一致。
>
> - 构建资产:`docs/build-env/docker/`(Dockerfile / apt-packages.txt / build-docker.sh / README.md)
> - 宿主机:Ubuntu 24.04 + docker-ce(容器内是 Ubuntu 22.04)
> - SDK:原厂 `T113-Tina5.0-V1.2.tar`(11G),全新解压,未复用任何旧 out/

## 1. 环境准备(宿主机)

宿主机需要:

1. docker-ce 已安装且在跑(`sudo systemctl status docker`),宿主用户在 docker 组:
   ```shell
   sudo usermod -aG docker $USER   # 重登生效
   ```
2. 磁盘 ≥60G 空闲(SDK 树 30G + out/ 6G + docker 镜像 1G + tar 11G)
3. 8 核 16G 以上(全量编译 kernel -j8)

> 坑 0.1(实测):docker 客户端配置若带死代理,会把 `http_proxy` 注入**所有容器**,
> 导致容器内 `apt-get`/`git` 全部连不上。检查并清理 `~/.docker/config.json`:
> ```json
> { "proxies": { "default": { "httpProxy": "http://x.x.x.x:7897", ... } } }
> ```
> 把整个 `proxies` 段删掉。另外 `/etc/systemd/system/docker.service.d/http-proxy.conf`
> 若配了死代理也要删,然后:
> ```shell
> sudo systemctl daemon-reload && sudo systemctl restart docker
> ```
> 实测环境 docker hub 直连不通,给 daemon 配国内 registry mirror
> (`/etc/docker/daemon.json`,实测生效):
> ```json
> { "registry-mirrors": ["https://docker.m.daocloud.io", "https://docker.1ms.run"], "dns": ["223.5.5.5", "119.29.29.29"] }
> ```

> 坑 0.2(实测):`~/.docker/buildx/.lock` 若被 root 拥有(以前 sudo docker 跑过),
> `docker build` 报 `open /home/xxx/.docker/buildx/.lock: permission denied`。
> `sudo chown -R $USER:$USER ~/.docker` 修复。

## 2. 构建编译镜像(约 20 分钟)

```shell
cd <sdk>/docs/build-env/docker
./build-docker.sh build-image
# 等价于:
# docker build --build-arg HOST_UID=$(id -u) --build-arg HOST_GID=$(id -g) \
#   -t t113-build:22.04 .
```

镜像内容(ubuntu:22.04 基线):

| 组件 | 说明 |
|---|---|
| 编译依赖 | `apt-packages.txt` 全量(build-essential/gcc-11/…,见 §5 踩坑记录) |
| 32 位支持 | `libc6-i386 lib32stdc++6 lib32z1 lib32ncurses6`(pack 的 fsbuild/dragon 需要) |
| uid/gid 直通 | builder 用户 = 宿主用户 uid/gid,bind-mount 下文件属主不乱 |
| git safe.directory | 容器内直接对挂载的 git 仓库操作 |

冒烟验证:

```shell
docker run --rm t113-build:22.04 bash -c "whoami && gcc --version | head -1 && python3 --version && ls /lib/ld-linux.so.2 && make --version | head -1"
# builder / gcc 11.4.0 / Python 3.10.12 / /lib/ld-linux.so.2 (32位loader在) / GNU Make 4.3
```

## 3. 从零全流程(容器内,约 5.5 小时)

### 3.1 解压 tar(容器内,~15 分钟)

```shell
docker run --rm \
  -v /path/docker_build:/work \
  -v /path/T113-Tina5.0-V1.2.tar:/input/T113-Tina5.0-V1.2.tar:ro \
  -w /work \
  t113-build:22.04 \
  tar -xf /input/T113-Tina5.0-V1.2.tar
```

> 解压后只有 `.repo/`,要按教程 `repo sync -l` 拉代码。

### 3.2 repo sync -l(容器内,~25 分钟)

Ubuntu 22.04 的 python 3.10 同样过不了捆绑 repo 1.24 的 `import imp`
(imp 在 3.5+ 被弃用、3.12 删除;实测 3.10 也没装)。按
`docs/peripheral/08-replication.md` §1.1 的四件套方案修(`.repo/repo/` 内):

1. 新增 `imp_compat.py`(new_module/load_source 垫片 + 自注册 `sys.modules['imp']`)
2. 新增启动器 `.repo/repo_py3`(注入 `--wrapper-version/--wrapper-path/--repo-dir` + `--` 分隔)
3. 修 4 处 py3 不兼容:
   - `platform_utils.py`:`from Queue import Queue` → try queue/Queue
   - `subcmds/help.py`:`from formatter import ...` → try/except + 文件内 shim 类
   - `git_config.py:_ReadGit`:`d.decode()` 对 str 加 `hasattr` 保护
   - `event_log.py`:`self._next_id.next()` → `next(self._next_id)`

然后:

```shell
docker run --rm -v <sdk>:/sdk -w /sdk t113-build:22.04 \
    .repo/repo_py3 sync -l
```

实测:`REPO_SYNC_DONE`,30G 全量拉出(brandy/bsp/build/device/kernel/openwrt/platform/prebuilt/rtos/tools)。

> 坑 3.2(实测):修 `git_config.py` 时用 heredoc 直接嵌 python 替换,`\0` 被 bash
> 转义吃掉导致补丁没生效。**改用独立 .py 文件执行替换**(转义经文件不受 shell 干扰)。
> 补丁后务必 `grep` 回验每一处。

### 3.3 按教程适配(容器内,~1 分钟)

教程的所有修改(BSP 复制、openwrt target、UART3、内核驱动、dts/config、包、rc.local)
在容器内脚本化执行,全部通过。要点:

- `docs/` 和 `tools/scripts/` 不在 repo 项目清单里(tar 里也没有),需单独补进树
- disclaimer 预置 `touch .disclaimer_accpet`(脚本环境不会停等交互)
- 全部适配项 = `ZGL_BOARD_Tutorial.md` 各章"修改内容"的等价落地

### 3.4 全量编译(容器内)

一条命令(即 `build-docker.sh zgl-build` 的内容):

```shell
docker run --rm -v <sdk>:/sdk -w /sdk t113-build:22.04 \
  bash -c "source build/envsetup.sh && \
           lunch t113-zgl_board && \
           ./build.sh bootloader && \
           ./build.sh kernel -j8 && \
           ./build.sh openwrt_rootfs -j1 && \
           ./build.sh pack"
```

实测耗时分解(8 核,openwrt 阶段固定 -j1):

| 阶段 | 耗时 |
|---|---|
| bootloader(brandy/u-boot) | ~10 分钟 |
| kernel(-j8) | ~25 分钟 |
| openwrt tools(其中 cmake bootstrap 串行 ~1.5h) | ~2 小时 |
| openwrt packages + target(-j1) | ~2.5 小时 |
| pack | ~5 分钟 |
| **合计** | **~5.5 小时** |

## 4. 实测踩坑记录(容器环境新增的 3 个)

### 4.1 U-Boot 编译报 `xxd: command not found`(build brandy Failed)

U-Boot `Makefile:1572` 用 `xxd -c 8 -i sunxi_challenge` 生成 C 数组,Ubuntu 22.04
的 `xxd` 在 `vim-common` 包里(默认容器没装)。**apt-packages.txt 已加**:
`xxd`、`bc`、`swig`、`libgnutls28-dev`(u-boot 常见依赖一并补齐)。

### 4.2 pack 报 `Tool busybox does not exist!`

`build/pack` 的 `maybe_busybox()` 用 `busybox` 包一层工具调用,宿主直编时系统有
busybox,容器里没有。**apt-packages.txt 已加 `busybox`**。

> 注意:加包后**重建镜像**(apt 层会重跑,约 15 分钟),已编好的
> bootloader/kernel/openwrt_rootfs 产物都在 SDK 树里,直接重跑 `./build.sh pack` 即可,
> 不必重来。

### 4.3 产物体积差异 ±2KB 属正常

| 版本 | 大小 | md5 |
|---|---|---|
| 宿主直编(Ubuntu 24.04) | 21153792 | 388f7f14122ec69602e785f83b94ecb8 |
| **Docker 22.04 容器内** | **21151744** | **1c4b281b1b97e6a650b451787c596e1e** |

差 2048 字节:镜像头部的时间戳字段不同,功能内容一致——rootfs 内关键产物
逐字节比对(见 §5)全部相同。

## 5. 产物验证(与宿主版逐项比对)

解包容器版 rootfs 核对(宿主机上 unsquashfs):

| 产物 | md5 | 与宿主版一致 |
|---|---|---|
| `usr/sbin/rtk_hciattach` | 98f83ebc2f78d28d281c7fa45088a2b7 | ✅ |
| `usr/bin/bt_test` | 32fe836aa7493f95468b53699617f741 | ✅ |
| `lib/libbtmg.so` | 2b8d26e3811a591773b8686322261dd5 | ✅ |
| `lib/modules/5.4.61/8723ds.ko` | 存在 | ✅ |
| `lib/firmware/rtlbt/rtl8723d_fw` 等 4 个 | 存在 | ✅ |
| `etc/rc.local`(BT 先 WiFi 后) | 命令行逐字一致 | ✅ |

内核镜像 `boot.img`/`uImage` 均产出;镜像头 `IMAGEWTY` 校验正常。

## 6. 日常使用(推荐工作流)

```shell
# 一次性:构建镜像
cd <sdk>/docs/build-env/docker && ./build-docker.sh build-image

# 日常:进交互 shell 编译(改动后增量,快)
./build-docker.sh shell /path/to/sdk
#   source build/envsetup.sh
#   lunch t113-zgl_board
#   ./build.sh kernel -j8 && ./build.sh pack

# 或者一键全流程(干净树从零)
./build-docker.sh zgl-build /path/to/sdk
```

## 6.1 后续验证:彻底去掉 tools/scripts/(2026-10-21 实测)

原 SDK 压缩包里**没有** `tools/scripts/`(setup-env.sh/sync_sdk.py 是历史辅助,不在
repo 项目清单里)。为贴近初学者真实路径,实测把 docker 流程对它的依赖**完全去除**:

- `apt-packages.txt` 增加 `python-is-python3`:容器裸 `python` → 3.10。
  背景:`build/mkcmd.sh:352/587/594` 在 **lunch 阶段**调裸 `python`(计算 defconfig
  相对路径),Ubuntu 默认无 `python` 命令,这是去脚本后的唯一硬依赖。
  SDK 自带 python3.8/make4.1 由 `openwrt/build/tinahooks_variable.mk:18` 在构建期
  自动接管;`build/envsetup.sh:18` 自己 export `TINA_TOPDIR`——都不依赖 setup-env.sh。
- 已从 SDK 树删除 `tools/scripts/` 后实测(2026-10-21,镜像 e1c3fac):
  - `source build/envsetup.sh && lunch t113-zgl_board` → 通过,无 python 报错
  - `./build.sh kernel -j8` + `./build.sh pack` → 通过
  - 产物 `t113_linux_zgl_board_uart0.img` 21151744 字节(与之前完全一致)

**结论:docker 环境下 SDK 压缩包解压即可用,无需补任何脚本。**

## 7. 结论

- **Ubuntu 22.04 容器内全流程(解压→sync→适配→bootloader→kernel→openwrt→pack)全部打通**,
  产物与 24.04 宿主直编功能等价,关键二进制逐字节一致
- 环境差异问题(4 类宿主依赖)被容器固化解决:任何人 clone 文档仓库、装 docker,
  `build-image` + `zgl-build` 两条命令即可得到一致结果
- 与宿主直编相比只多了容器开销(首轮 apt + 镜像 ~20 分钟),编译时长本身相当
