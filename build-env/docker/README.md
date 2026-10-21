# T113 SDK Docker 可复现编译环境

> 解决"不同人 Ubuntu 版本不一样(20.04 / 22.04 / 24.04),编译 SDK 遇到不同报错"的问题。
> 统一在 **Ubuntu 22.04 容器**里编译;SDK 源码用 bind-mount 挂载,不进镜像。
>
> **✅ 已实测全流程验证**(2026-10-20,从解压原厂 tar 到产出镜像,与宿主直编结果一致):
> 完整踩坑实录见 [REPLICATION_LOG.md](REPLICATION_LOG.md)。
>
> **学员入口:从零部署+编译+测试的 copy-paste 手册见 [QUICK_START.md](QUICK_START.md)**
> (git clone 仓库 → 装 docker → 下载 SDK → 编译 → 烧录 → 7 外设测试,逐条命令)。

## 1. 为什么需要容器(版本差异根源)

| 依赖 | 说明 | 踩坑表现 |
|---|---|---|
| 32 位 pack 工具 | `tools/pack/pctools/linux/` 下 17 个 i386 ELF(fsbuild/dragon/update_23 等),需要 `libc6-i386 + lib32stdc++6` | pack 报 `No such file or directory`(其实是缺 32 位 loader) |
| Python 版本窗口 | OpenWrt `prereq-build.mk` 只接受 3.5–3.10 且要 `distutils` | Ubuntu 24.04(python 3.12)报 prereq failed / `No module named distutils` |
| 宿主 gcc/g++ ≥4.8、make ≥3.82 | OpenWrt 首轮 prereq 硬检查(SDK 不带 host gcc) | 老系统 gcc 过老直接 fail |
| repo 工具 | SDK 捆绑 repo 1.24 是 Python 2 时代(`import imp`) | 24.04 上 `repo sync` 直接崩(见 `docs/peripheral/08-replication.md` §1.1) |

SDK 自带的 `prebuilt/hostbuilt/`(make4.1、python3.8、36 个 host 工具)和
目标工具链(linaro/sunxi-glibc)**都是自包含的**,只要容器基础系统版本正确,其余全部 hermetic。

## 2. 快速开始

前置:宿主机已装 docker,宿主用户在 `docker` 组(`groups` 看得到,否则命令前加 sudo)。

```shell
cd <sdk>/docs/build-env/docker

# 1) 构建镜像(首次约 5 分钟,自动带宿主 uid/gid)
./build-docker.sh build-image

# 2) 一键从零编译(等价于 envsetup + lunch + kernel + openwrt + pack)
./build-docker.sh zgl-build /home/xxx/T113-Tina5.0-V1.2
# SDK 目录不传则自动用本脚本向上三层的 SDK 树

# 产物:<sdk>/out/t113_linux_zgl_board_uart0.img
```

> **无需任何预置脚本**。Ubuntu 22.04 容器自带 python 3.10(在 OpenWrt prereq 窗口内,
> 由 python-is-python3 包提供裸 `python` 命令),SDK 自带的 python3.8/make4.1
> 由构建系统自动接管——原 SDK 压缩包里没有的 `tools/scripts/setup-env.sh` 不需要,
> 容器环境已经适配好。

国内网络可换源构建镜像:

```shell
docker build --build-arg APT_MIRROR=mirrors.aliyun.com -t t113-build:22.04 .
```

## 3. 常用命令

```shell
# 进交互编译 shell(推荐日常用这个,里面按教程正常操作)
./build-docker.sh shell /home/xxx/T113-Tina5.0-V1.2
# 容器内:
#   source build/envsetup.sh
#   lunch t113-zgl_board
#   ./build.sh kernel -j8

# 单条命令(自动 pty 包装,disclaimer 已自动预置)
./build-docker.sh run "./build.sh kernel -j8" /home/xxx/T113-Tina5.0-V1.2
```

## 4. 与宿主直编的差异

- 容器内生成文件属主 = 宿主用户(uid/gid 在镜像构建时直通),out/ 不会出现 root 隶属文件
- `--network host`:代理/网络与宿主一致;`http_proxy` 等环境变量自动透传
- 全志 build 脚本依赖交互终端,`run`/`zgl-build` 模式自动用 `script -qec` 做 pty 包装,日志落在容器内 `/tmp/*.log`(临时,要看日志请用 `shell` 模式或重定向)
- 首次构建的免责声明通过预置 `<sdk>/.disclaimer_accpet` 跳过(脚本自动做)
- **adb 烧录测试仍在宿主机做**(容器只负责编译)

## 5. 换基础镜像(20.04 / 24.04)

```shell
# 20.04(更老,apt 包旧但官方指南时代最接近)
docker build --build-arg BASE=ubuntu:20.04 ...
# 24.04(需处理 python3.12:SDK 自带 python3.8 可用,但 repo 工具需 08-replication.md §1.1 的垫片)
```

> 实测基线是 22.04;20.04 理论可用(包更全),24.04 需额外垫片,建议统一 22.04。

## 6. 常见问题

| 现象 | 处理 |
|---|---|
| `permission denied ... /var/run/docker.sock` | 宿主用户加 docker 组:`sudo usermod -aG docker $USER` 后重登 |
| 容器内 `git status` 报 dubious ownership | 镜像已配 `safe.directory '*'`;若仍报,在容器内 `git config --global --add safe.directory '*'` |
| 磁盘不足 | SDK 树 + 镜像约 40G,`docker system df` 查看;清理 `docker builder prune` |
| 下载 openwrt 源码包超时 | 容器走宿主网络;或手动下到 `<sdk>/openwrt/dl/`(教程触摸章有 evtest 示例) |
| 编译卡在免责声明 | 脚本已自动 `touch .disclaimer_accpet`;手动方式同 |
| 容器内 apt 下载全失败 | 检查 `~/.docker/config.json` 的 `proxies` 段与 daemon 代理配置是否指向死代理(见 REPLICATION_LOG.md §1) |
| U-Boot 报 `xxd: command not found` | apt-packages.txt 已含 xxd/bc/swig,重建镜像即可 |
| pack 报 `Tool busybox does not exist!` | apt-packages.txt 已含 busybox,重建镜像后直接重跑 pack(不必重编前面的) |
