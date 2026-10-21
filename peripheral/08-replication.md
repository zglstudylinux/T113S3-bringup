# 从零复刻记录(new_build_t113)

> 本文档记录按 `docs/Tutorial_Doc/ZGL_BOARD_Tutorial.md` 从零复刻 zgl_board 适配时
> 遇到的**环境/工具问题**和解决方法。外设适配内容本身见教程正文,这里只记"教程之外"的坑。
>
> 复刻目录:`/home/zgl/Linux_SDK/T113/new_build_t113/T113-Tina5.0-V1.2`

## 1. 解压与拉取代码

### 1.1 `repo sync -l` 在 Python 3.12 上失败(环境问题,教程外的第一个坑)

> **适用范围(2026-10-21 实测更正)**:本节的 imp 垫片 + repo_py3 启动器方案只
> **python 3.12**(Ubuntu 24.04 宿主直编)需要——3.12 删除了 imp 模块。
> **Python 3.10(Ubuntu 22.04 容器)里 imp 还在**(只是 DeprecationWarning),
> 官方 `.repo/repo/repo` 直接可用,只需修 4 处:`platform_utils.py` 的 Queue、
> `subcmds/help.py` 的 formatter、`git_config.py` 的 decode、`event_log.py` 的
> next()——见 `docs/build-env/docker/QUICK_START.md` 第 5 步(22.04 容器的标准流程)。

**现象**:解压后按教程执行 `.repo/repo/repo sync -l`,报:

```
ModuleNotFoundError: No module named 'imp'
```

**原因**:SDK 捆绑的 repo 是 1.24(2014 年,Python 2 时代),`main.py`/`project.py` 等
直接 `import imp`;`imp` 模块在 Python 3.12(Ubuntu 24.04 默认)已被删除。
主机没有 python2,只有 `python3.12`。

**解决方法**(全部在 `.repo/` 内完成,不动 SDK 代码仓库):

1. 写一个 `imp` 兼容垫片 `.repo/repo/imp_compat.py`,提供捆绑 repo 用到的仅有的两个
   函数 `new_module()`/`load_source()`(`load_source` 用 `SourceFileLoader` 显式构造,
   因为 `repo` 启动脚本没有扩展名,`spec_from_file_location` 默认返回 None):

```python
import importlib.util
import sys
import types

def new_module(name):
    return types.ModuleType(name)

def load_source(modname, filepath):
    from importlib.machinery import SourceFileLoader
    loader = SourceFileLoader(modname, filepath)
    spec = importlib.util.spec_from_file_location(modname, filepath, loader=loader)
    module = importlib.util.module_from_spec(spec)
    sys.modules[modname] = module
    loader.exec_module(module)
    return module

sys.modules.setdefault('imp', sys.modules[__name__])
```

2. 写一个 Python 3 启动器 `.repo/repo_py3` 替代 `repo` 命令(注入捆绑 repo 必需的
   启动参数,并用 `--` 分隔符保护 `sync -l` 等子命令参数不被 `_PruneOptions` 删掉):

```python
#!/usr/bin/env python3
import sys, os
_HERE = os.path.dirname(os.path.realpath(__file__))          # <sdk>/.repo
_REPO = os.path.join(_HERE, 'repo')                          # <sdk>/.repo/repo
sys.path.insert(0, _HERE)
sys.path.insert(0, _REPO)
import imp_compat  # registers itself as sys.modules['imp']

_argv = sys.argv[1:]
if not any(a.startswith('--wrapper-version') for a in _argv):
    _argv = ['--wrapper-version=1.24',
             '--wrapper-path=' + os.path.join(_REPO, 'repo'),
             '--repo-dir=' + _HERE, '--'] + _argv
sys.argv = [sys.argv[0]] + _argv

_g = {'__name__': '__main__', '__file__': os.path.join(_REPO, 'main.py')}
exec(open(os.path.join(_REPO, 'main.py')).read(), _g)
```

3. 修捆绑 repo 源码里另外 4 处 Python 3 不兼容(`.repo/repo/` 内,repo 工具代码,
   不属于任何 SDK git 仓库):

| 文件 | 问题 | 修法 |
|---|---|---|
| `platform_utils.py:23` | `from Queue import Queue` | `try: from queue import Queue except ImportError: from Queue import Queue` |
| `subcmds/help.py:19` | `from formatter import ...`(3.10 删除) | try/except 回退到文件内定义的 shim 类 |
| `git_config.py:_ReadGit` | `d.decode('utf-8')` 对 str 调 decode | `if hasattr(d, 'decode'): d = d.decode('utf-8')` |
| `event_log.py:74` | `self._next_id.next()`(py2 生成器协议) | `next(self._next_id)` |

> 注意:`--wrapper-version`/`--wrapper-path`/`--repo-dir` 三个参数必须带 `--` 分隔符
> 注入,否则 `_PruneOptions` 会把 `sync -l` 当"未注册选项"删掉,repo 只打印 help。

**结果**:`.repo/repo_py3 sync -l` 成功拉出全部 122 个项目(30G)。

### 1.2 docs/ 不在 tar 的 repo 项目里

tar 的 `.repo/project.list` 只有 122 个工程(不含 docs/),按教程从零复刻时需要
单独把教程文档拷进新树(本记录写于复刻过程,docs 从参考树复制)。

## 2. 编译与适配过程

### 2.1 `tools/scripts/` 目录缺失(setup-env.sh 找不到)

**现象**:按教程 `. tools/scripts/setup-env.sh` 报 `No such file or directory`。

**原因**:tar 只包含 repo 项目清单里的内容,`tools/scripts/`(setup-env.sh/sync_sdk.py)
不在任何 repo 工程里,解压后不存在。

**解决方法**:从参考树拷贝 `cp -r <参考树>/tools/scripts <新树>/tools/`。
(同理 docs/ 也从参考树拷贝。)

### 2.2 首次构建卡死:disclaimer 免责声明交互

**现象**:`./build.sh kernel -j8` 卡在 `Jump to longan autoconfig` 后无输出。
`ps` 查看是 `build/disclaimer/disclaimer.sh` 在等交互输入(后台 script 会话里永远等不到)。

**解决方法**:把参考树的 `.disclaimer_accpet` 标记文件拷到新树根目录:

```shell
cp <参考树>/.disclaimer_accpet <新树>/.disclaimer_accpet
```

> 注意:第一次卡死时杀掉 build 再重启前,要确认旧 disclaimer.sh 进程也杀干净,
> 否则会留一个空转的孤儿进程。

### 2.3 defconfig 的隐藏坑:menuconfig 残留项

教程只列了要改的包行,直接 sed 改完后有 4 处和"参考成品 defconfig"语义不一致,
逐一修正:

| 残留项 | 现象 | 修正 |
|---|---|---|
| `CONFIG_SUNXI_ALLOC_IOMMU=y` | 旧显示方案符号,新树 Kconfig 里已不存在 | 换成 `CONFIG_SUNXI_TRANS_DISPBUF_FD=y`(libuapi 默认) |
| `LIBEXPAT_BUILD_BOTH` 缺失 | 参考 defconfig 有 | 加上 `CONFIG_LIBEXPAT_BUILD_BOTH=y` |
| `CONFIG_WMG_DEMO_TEST_TOOL` 缺失 | wifimanager 测试工具 | 加上 `=y` |
| `CONFIG_PACKAGE_bluez-utils-btmon` 缺失 | bluez 监控工具 | 加上 `=y` |

校验方法(语义级比较,忽略菜单排序差异):

```shell
diff <(grep -E "^CONFIG.*=y$" <参考树>/.../defconfig | sort) \
     <(grep -E "^CONFIG.*=y$" <新树>/.../defconfig | sort)
```

### 2.4 evtest 源码包下载失败(死代理)

**现象**:编译 evtest 时 `curl` 反复重连 `192.168.32.153:7897`(环境代理已失效)超时。

**解决方法**(教程提示的方案):绕过代理手动下载放进 `openwrt/dl/`:

```shell
env -u http_proxy -u https_proxy curl -fL \
  -o openwrt/dl/evtest-1.34.tar.xz \
  http://deb.debian.org/debian/pool/main/e/evtest/evtest_1.34.orig.tar.xz
rm -f openwrt/dl/evtest-1.34.tar.xz.dl openwrt/dl/evtest-1.34.tar.xz.hash
```

sha256 校验 = `e49f1f160b30c8f7c2a4caef5ab655f1caf816483d19fdedd6db2d251d7ab80e`
(与 Makefile PKG_HASH 一致),放好后重新编译自动取本地包。

### 2.5 pack 报 `u-boot.fex cant be open`(bootloader 没先编译)

**现象**:`./build.sh kernel && ./build.sh openwrt_rootfs && ./build.sh pack` 一路通过,
最后 pack 报 `file .../pack_out/u-boot.fex cant be open → create package failed`
(此前有 `split uboot and fdt failed` 前兆)。

**原因**:pack 不会自己编 bootloader;`u-boot.fex` 来自 `./build.sh bootloader` 的产物
(tar 新树从未编过 bootloader)。

**解决方法**:

```shell
./build.sh bootloader    # 约 5 分钟,build brandy OK
./build.sh pack          # 重新打包成功
```

### 2.6 产物自检(rootfs 解包核对)

打包完成后解开 rootfs 核对全部外设产物(和参考成品逐项一致):

| 产物 | 位置 | 校验 |
|---|---|---|
| rc.local BT 先 WiFi 后 | `etc/rc.local` | `rtk_hciattach ... rtk_h5` 在 `modprobe 8723ds` 之前 |
| rtk_hciattach | `usr/sbin/rtk_hciattach` | 39092B(rstrip 后)md5 `98f83ebc...`,与参考树根文件系内二进制逐字节一致(打包 rstrip 确定性的体现) |
| bt_test | `usr/bin/bt_test` | md5 `32fe836a7493f95468b53699617f741`(锁版 4.0.3) |
| libbtmg.so | `lib/libbtmg.so` | md5 `2b8d26e3811a591773b8686322261dd5`(锁版 4.0.3) |
| 8723ds.ko | `lib/modules/5.4.61/8723ds.ko` | 存在 |
| 蓝牙固件 | `lib/firmware/rtlbt/rtl8723d_{fw,config}` | 存在 |
| 音频路由 | `etc/asound.conf` | 存在(SDK 自带) |

> rstrip 说明:ipkg 打包时 `rstrip.sh` 会剥调试符号,100096B 的 prebuilt 变成 39092B,
> md5 随之变化;与参考树成品 rootfs 内的文件完全一致即为正确(实机 ACL MTU 1021
> 是最终判据)。

## 3. 板上实测验证(2026-10-20,新树镜像烧录后)

用户实测通过:串口(UART3 启动日志)、屏幕(雪花/彩条)、触摸(evtest)、麦克风(arecord/aplay 回放)。
WiFi + 蓝牙由 adb 按教程命令实测,全部通过:

### 3.1 WiFi

| 项 | 实测 |
|---|---|
| `/proc/misc` | `200 sunxi-wlan` |
| 8723ds 开机加载 | rc.local 时序生效(BT 后),fw_ver=31 下载成功 |
| SDIO 枚举判据 | dmesg **无** `mmc1` 行;判据为 `RTW: == SDIO Card Info ==` + `RTW: CHIP TYPE: RTL8723D`(教程已修正) |
| 连接 zgl(WPA2-PSK) | `wpa_state=COMPLETED`,CCMP |
| DHCP | `10.25.28.164` |
| ping 网关 / 223.5.5.5 | 均 0% packet loss |
| 信号 | -42 dBm,72.2 MBit/s |

### 3.2 蓝牙

| 项 | 实测 |
|---|---|
| `/tmp/bt_boot.log` | ROM 态 0x08/0x8723 → IC: RTL8723DS → Config baud 04928002 → **BT up after 2s** → 8723ds loaded (after BT) |
| hciconfig hci0 | `ACL MTU: 1021:8`(锁版特征),`UP RUNNING PSCAN ISCAN` |
| Name / Class | `aw-bt-test-D1-A3` / `0x040400`(btmanager 4.0.3 回调标志) |
| 手机配对 | 免 PIN 直连成功 |
| A2DP 播放 | `state: RUNNING`,hw_ptr 持续增长(48kHz S16_LE 立体声);RX acl 包 3507→5141 持续增长;`hcitool con` 显示 `AUTH ENCRYPT` |
| 音质 | 用户确认正常 |

**结论**:按教程从零复刻的新树产物功能与参考树完全一致,`ZGL_BOARD_Tutorial.md` 验证有效。
最终镜像 md5 `388f7f14122ec69602e785f83b94ecb8`(21153792 字节)。

## 附:复刻产物镜像

| 阶段 | 镜像 md5(`out/t113_linux_zgl_board_uart0.img`) |
|---|---|
| 最终镜像(21153792 字节,2026-10-20) | `388f7f14122ec69602e785f83b94ecb8` |

## 附 2:编译环境容器化(2026-10-20 实测)

本篇 §1.1/§2.1~2.5 的环境坑(repo py3、tools/scripts、disclaimer、defconfig 残留、
evtest 下载、bootloader)已全部通过 **Ubuntu 22.04 Docker 容器**固化解决——
任何人换 Ubuntu 版本都能得到一致结果。构建资产与踩坑实录:

- 使用文档:`docs/build-env/docker/README.md`
- 实测全记录:`docs/build-env/docker/REPLICATION_LOG.md`
  (从解压原厂 tar 到产出镜像 ~5.5h,关键二进制与宿主直编逐字节一致)
