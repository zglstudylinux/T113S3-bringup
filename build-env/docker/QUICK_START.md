# T113 ZGL_BOARD 从零部署·编译·测试手册(Docker 版)

> 面向学员的完整 copy-paste 手册。学员拿到的资料只有三样:
> 1. **docs 手册包**(网盘提供,含本文件、ZGL_BOARD_Tutorial.md、外设测试记录)
> 2. **官方 SDK 压缩包** `T113-Tina5.0-V1.2.tar`(网盘提供)
> 3. **自己搭的 Ubuntu 20.04 / 22.04 / 24.04 宿主机**
>
> 按本手册从第 0 步开始逐条复制命令执行,即可完成:部署 Docker → 构建编译镜像 →
> 解压 SDK → 从零适配板级 → 全量编译 → 烧录 → 7 个外设逐项测试。
>
> 宿主机版本不重要——所有编译都在 Ubuntu 22.04 容器里完成,结果一致
> (实测记录见 `REPLICATION_LOG.md`)。

---

## 第 0 步 宿主机前置(一次性)

要求:x86_64,磁盘空闲 ≥ 60G(SDK 树 30G + out/ 6G + Docker 镜像 1G + tar 11G),8 核 16G 以上更快。

### 0.1 先确认网络没有失效代理(实测踩坑)

如果 `apt`/`curl` 报 `Could not connect to 192.168.32.153:7897`(或任何 `Connection refused`),
说明系统配了失效代理——先清掉再装包(按配了哪个清哪个):

```shell
# 查哪里配了代理
env | grep -i proxy                                   # 当前 shell 环境变量
grep -r "proxy" /etc/apt/apt.conf.d/ 2>/dev/null      # apt 配置
cat /etc/environment 2>/dev/null | grep -i proxy      # 系统环境

# 清 apt 代理(有这个文件才需要)
sudo rm -f /etc/apt/apt.conf.d/*proxy*
# 清环境变量代理: 编辑 /etc/environment 删掉 proxy 行,或当前会话:
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
```

### 0.2 安装 docker(Ubuntu 20/22/24 通用)

```shell
sudo apt update
# 二选一:
sudo apt install -y docker.io docker-buildx    # Ubuntu 源(推荐,简单)
# 若之前装过 docker-ce/containerd 报 "Conflicts: containerd",改用官方源方式,
# 或先清残留: sudo apt remove -y docker-ce containerd containerd.io && sudo apt autoremove
sudo systemctl enable --now docker
```

> 坑(实测):`docker.io` 会拉 `containerd.io`,与手动装过的 `containerd` 冲突
> (`Conflicts: containerd`)。新机器无此问题;有残留的机器先 `sudo apt remove -y
> containerd containerd.io` 再装。

```shell
# 把当前用户加进 docker 组(免 sudo 用 docker),执行后要重新登录一次
sudo usermod -aG docker $USER
```

# 把当前用户加进 docker 组(免 sudo 用 docker),执行后要重新登录一次
sudo usermod -aG docker $USER
```

重新登录后验证(不要带 sudo):

```shell
docker run --rm hello-world
# 预期: Hello from Docker!
```

> 依赖包版本核对:SDK 自带 python3.8/make4.1,容器自带 gcc/make/python3.10,
> 宿主机只需要 docker 本身。

---

## 第 1 步 获取构建资产(文档仓库)

```shell
mkdir -p ~/t113 && cd ~/t113
git clone https://github.com/zglstudylinux/T113S3-bringup.git
ls T113S3-bringup/build-env/docker/
# 预期: apt-packages.txt  build-docker.sh  Dockerfile  README.md  REPLICATION_LOG.md  QUICK_START.md
```

---

## 第 2 步 下载 SDK 压缩包

从网盘下载 `T113-Tina5.0-V1.2.tar` 放到 `~/t113/`,校验完整性:

```shell
cd ~/t113
md5sum T113-Tina5.0-V1.2.tar
# 预期(与网盘页面公布的 md5 一致): <SDK_TAR_MD5>
ls -lh T113-Tina5.0-V1.2.tar
# 预期: 约 11G
```

> 网盘同时提供 `docs 手册包`,其中包含 `ZGL_BOARD_Tutorial.md`(外设适配教程)、
> 各外设测试记录(`peripheral/`)和本手册的更新版。把 docs 包解压到 `~/t113/docs/`
> 即可在后续步骤引用。

---

## 第 3 步 构建编译镜像(约 20 分钟)

```shell
cd ~/t113/T113S3-bringup/build-env/docker
./build-docker.sh build-image
```

国内网络建议换源(二选一,上面那个失败再用这个):

```shell
docker build --build-arg HOST_UID=$(id -u) --build-arg HOST_GID=$(id -g) \
  --build-arg APT_MIRROR=mirrors.aliyun.com \
  -t t113-build:22.04 .
```

验证镜像:

```shell
docker images | grep t113-build
# 预期: t113-build   22.04   ...   约 820MB

docker run --rm t113-build:22.04 bash -c "whoami && python --version && gcc --version | head -1 && ls /lib/ld-linux.so.2 && which busybox"
# 预期: builder / Python 3.10.12 / gcc 11.4.0 / /lib/ld-linux.so.2(32位loader在) / /usr/bin/busybox
```

> 若宿主机 docker pull 拉不下 ubuntu:22.04:给 docker daemon 配国内 registry mirror
> (`/etc/docker/daemon.json`,然后 `sudo systemctl restart docker`):
> `{ "registry-mirrors": ["https://docker.m.daocloud.io", "https://docker.1ms.run"], "dns": ["223.5.5.5", "119.29.29.29"] }`
> 同时检查 `~/.docker/config.json` 和 `/etc/systemd/system/docker.service.d/` 里有没有
> 失效代理配置(`proxies` 段 / `http-proxy.conf`),有则删除。

---

## 第 4 步 解压 SDK(容器内,约 15 分钟)

```shell
mkdir -p ~/t113/sdk
docker run --rm \
  -v ~/t113/sdk:/work \
  -v ~/t113/T113-Tina5.0-V1.2.tar:/input/T113-Tina5.0-V1.2.tar:ro \
  -w /work \
  t113-build:22.04 \
  tar -xf /input/T113-Tina5.0-V1.2.tar
ls ~/t113/sdk/T113-Tina5.0-V1.2/
# 预期: 只有一个 .repo 目录(代码还没拉出来),下一步拉取
```

---

## 第 5 步 repo sync 拉取代码(容器内,约 25 分钟)

**为什么这一步必须有**:SDK 不是一个大目录,而是 **122 个独立 git 仓库**
(kernel/device/openwrt/brandy/platform...)由官方 `repo` 工具统一管理。
解压 tar 后只有 `.repo/` 里的对象库,**工作树(看得见的 kernel/、device/ 等目录)
要靠 `repo sync -l` 从对象库 checkout 出来**——这一步是 SDK 官方流程,不可跳过、
不可替代(没有 repo 就没有完整源码树)。`repo` 命令在 SDK 压缩包里自带
(`.repo/repo/repo`),不是额外安装的东西。

SDK 捆绑的 repo 工具(2014 年版)有 4 处 Python 3 不兼容,需要先打 4 个小补丁。
**为什么打补丁**:22.04 容器的 python 3.10 里,`Queue`/`formatter` 标准模块已被移除、
repo 源码还有 2 处 py2 写法(str decode / 生成器 next)——这些与 docker 无关,
是 SDK 自带 repo 工具 2014 年代码的固有问题,Ubuntu 20/22/24 宿主直编同样会遇到。
补丁只改 `.repo/repo/` 内的 repo 工具源码,不碰任何 SDK git 仓库。

(下面按 5.1 / 5.2 / 5.3 **三个命令块分开复制执行**;**不需要** imp 垫片——
python 3.10 里 imp 还在,用官方 `.repo/repo/repo` 直接跑):

### 5.1 写补丁脚本到 /tmp(整块复制,粘贴后回车,无输出)

```shell
cat > /tmp/fix_repo.py << 'EOF'
import sys
sdk = sys.argv[1]

# 1. platform_utils.py: Queue 模块(py3 改名 queue)
p = sdk + '/.repo/repo/platform_utils.py'
src = open(p).read()
if 'from queue import Queue' not in src:
    src = src.replace("from Queue import Queue",
"""try:
  from queue import Queue  # Python 3
except ImportError:
  from Queue import Queue  # Python 2""", 1)
    open(p, 'w').write(src)
    print('platform_utils OK')

# 2. subcmds/help.py: formatter 模块(py3.10 删除,用文件内 shim 等价类)
p = sdk + '/.repo/repo/subcmds/help.py'
src = open(p).read()
if 'except ImportError:' not in src.split('class Help')[0]:
    shim = '''try:
  from formatter import AbstractFormatter, DumbWriter  # Python <=3.9
except ImportError:
  import io

  class DumbWriter(object):
    def __init__(self, file=None, maxcol=72):
      self.file = file or io.StringIO()
      self.maxcol = maxcol
    def write(self, data):
      self.file.write(data)
    def flush(self):
      pass
    def reset(self):
      pass
    def send_literal_data(self, data):
      self.write(data)
    def send_flowing_data(self, data):
      self.write(data)

  class AbstractFormatter(object):
    def __init__(self, writer):
      self.writer = writer
    def flush(self):
      pass
    def add_flowing_data(self, data):
      self.writer.send_flowing_data(data)
    def add_literal_data(self, data):
      self.writer.send_literal_data(data)
    def format_counter(self, format, counter):
      pass
    def format_label(self, format, counter):
      pass
    def end_paragraph(self, blankline):
      pass
    def push_style(self, *styles):
      pass
    def pop_style(self, n=1):
      pass
    def add_hor_rule(self, *args, **kw):
      pass
    def end_element(self, method=None):
      pass
    def start_element(self, tag, attrs=None, method=None):
      pass
    def assert_empty(self, tags):
      pass'''
    src = src.replace("from formatter import AbstractFormatter, DumbWriter", shim, 1)
    open(p, 'w').write(src)
    print('help.py OK')

# 3. git_config.py: py2 的 bytes.decode 在 py3 subprocess 返回 str 时报错
p = sdk + '/.repo/repo/git_config.py'
src = open(p).read()
old = "    for line in d.decode('utf-8').rstrip('\\0').split('\\0'):"
if old in src:
    new = "    if hasattr(d, 'decode'):\n      d = d.decode('utf-8')\n    for line in d.rstrip('\\0').split('\\0'):"
    src = src.replace(old, new, 1)
    open(p, 'w').write(src)
    print('git_config OK')

# 4. event_log.py: py2 生成器协议 .next() 在 py3 改为内置 next()
p = sdk + '/.repo/repo/event_log.py'
src = open(p).read()
if 'next(self._next_id)' not in src:
    src = src.replace("'id': (kind, self._next_id.next()),",
                      "'id': (kind, next(self._next_id)),", 1)
    open(p, 'w').write(src)
    print('event_log OK')
EOF
```

### 5.2 执行补丁(复制运行,预期 4 行 OK)

```shell
SDK=~/t113/sdk/T113-Tina5.0-V1.2
python3 /tmp/fix_repo.py $SDK
# 预期: platform_utils OK / help.py OK / git_config OK / event_log OK (四行)
# 少任何一行都不要往下走,把输出贴出来排查
```

### 5.3 拉取代码(约 25 分钟)

```shell
docker run --rm \
  -v ~/t113/sdk/T113-Tina5.0-V1.2:/sdk \
  -w /sdk \
  t113-build:22.04 \
  bash -c ".repo/repo/repo sync -l && echo REPO_SYNC_DONE"
# 预期: 开头一行 "warning: Python 3 support is currently experimental"(正常),
# 大量 "Updating files: 100%" 后出现 REPO_SYNC_DONE

# 完成后验证:
ls ~/t113/sdk/T113-Tina5.0-V1.2/
# 预期: brandy  bsp  build  buildroot  build.sh  device  kernel  openwrt  platform  prebuilt  rtos  tools
```

> 注意:`tools/scripts/`、`docs/` 不在 SDK 的 repo 项目清单里,默认解压不会有——
> **docker 环境不需要它们**(build-docker.sh 已适配,无需 setup-env.sh;docs 从网盘
> 手册包获取)。

---

## 第 6 步 按教程适配板级(BSP/内核/包)

SDK 拉出来后是 `evb1_auto_nand` 原厂板,需要按 `ZGL_BOARD_Tutorial.md`
(网盘 docs 手册包里)完成 zgl_board 适配,共 7 个部分:

| 教程章节 | 内容 | 涉及 |
|---|---|---|
| 创建和配置板级支持包 | `cp -r evb1_auto_nand zgl_board` + machine 改名 | device/config/chips/t113 |
| 添加 openwrt 支持 | `cp -r t113-evb1_auto_nand t113-zgl_board` + 4 个文件改名/替换(**Makefile 的 `BOARD:=`/`BOARDNAME:=` 必须改成 t113-zgl_board**——漏改会让 targetinfo 不认识新目标,menuconfig 时 .config 被重置成默认板,编译直接失败) | openwrt/target/t113 |
| 串口(UART3) | sys_config.fex / board.dts / env.cfg | device + board.dts |
| MIPI 屏 | st7701sn 驱动新增 + panels/Makefile/Kconfig + dts | kernel |
| 触摸 | dx_touch 驱动新增 + Kconfig/Makefile + dts | kernel |
| WiFi | rfkill/wlan 两处修复 + config + dts + kmod 包 | kernel + openwrt |
| 蓝牙 | H5 配置 + dts + 锁版二进制 + rc.local 时序 | kernel + openwrt + platform |

逐章按教程的"diff + 修改后最终内容(直接复制)"块操作即可。

> 提示:全量手工 vi 一次约 1~2 小时;如果你拿到的 docs 手册包里附带了
> `adapt_zgl_board.sh` 适配脚本(网盘 tools/ 目录),也可以先手工跟一遍教程理解
> 每处修改,再用脚本核对结果。

---

## 第 7 步 全量编译(容器内,约 5.5 小时)

一条命令完成 bootloader + kernel + openwrt + pack:

```shell
cd ~/t113/T113S3-bringup/build-env/docker
./build-docker.sh zgl-build ~/t113/sdk/T113-Tina5.0-V1.2
# 流程: envsetup -> lunch t113-zgl_board -> kernel(-j8) -> openwrt_rootfs(-j1) -> pack
```

交互方式(推荐,可随时查看输出):

```shell
./build-docker.sh shell ~/t113/sdk/T113-Tina5.0-V1.2
# 进入容器后:
#   source build/envsetup.sh
#   lunch t113-zgl_board
#   ./build.sh bootloader      # 首次必须;pack 不自己编 bootloader
#   ./build.sh kernel -j8
#   ./build.sh openwrt_rootfs -j1    # 必须 -j1,并行会踩全志 host 工具 jobserver 坑
#   ./build.sh pack
```

产物:`~/t113/sdk/T113-Tina5.0-V1.2/out/t113_linux_zgl_board_uart0.img`(约 21MB)

---

## 第 8 步 验收镜像

```shell
IMG=~/t113/sdk/T113-Tina5.0-V1.2/out/t113_linux_zgl_board_uart0.img
test -s $IMG && echo "IMAGE OK: $(stat -c%s $IMG) bytes"
md5sum $IMG
# 预期: 21151744 字节(±2KB 属正常,时间戳差异)
# 参考值(±2KB 属正常,时间戳差异):
# 1c4b281b1b97e6a650b451787c596e1e(2026-10-20 实测,Ubuntu 24.04 宿主 + 22.04 容器)
# f3df692f60d2ee6ee14b879b2786880a(2026-10-21 实测,Ubuntu 24.04 宿主 + 22.04 容器,全新学员流程)
```

rootfs 内容自检(可选):

```shell
unsquashfs -l ~/t113/sdk/T113-Tina5.0-V1.2/out/t113/zgl_board/openwrt/rootfs.img | grep -E "rtlbt|8723ds.ko|rtk_hciattach|bt_test|libbtmg"
# 预期: 8723ds.ko + rtlbt 4 个固件 + rtk_hciattach + bt_test + libbtmg.so 全部在列
```

---

## 第 9 步 烧录(宿主机)

- **TF 卡烧写:用全志 PhoenixCard**。两个要点:
  1. 一定要选择烧写的卡是**启动卡**,不是默认的**量产卡**(选错会 boot0 读不到介质,报
     `MMC init failed → spinand read id failed: -110 → RESET` 死循环);
  2. PhoenixSuit 是 flash(nand/emmc)烧写工具,**不要**用它烧 TF 卡。
- 烧卡后 TF 卡插板卡上电。

---

## 第 10 步 外设逐项测试(adb + 串口,预期全部实测通过)

> 调试口是 **UART3(PB6/PB7,U24 3P 座)**,115200。adb 用 OTG 口。

### 10.1 串口

```shell
# 看内核时间戳确认烧的是新镜像
adb shell 'uname -a'
# 预期: Linux TinaLinux 5.4.61 ... SMP PREEMPT <编译日期>
adb shell 'cat /proc/cmdline | grep -o "console=ttyS3,115200 earlycon=uart8250,mmio32,0x02500c00"'
# 预期: console=ttyS3,115200 earlycon=uart8250,mmio32,0x02500c00
```

### 10.2 屏幕(480x640 MIPI)

```shell
adb shell 'cat /dev/urandom > /dev/fb0'
# 预期: 屏幕全部雪花;最后报 cat: write error: No space left on device(正常,写满即停)

adb shell 'cat /dev/zero > /dev/fb0'      # 先清屏(必须,不然残留影响彩条)
adb shell 'echo 8 > /sys/class/disp/disp/attr/colorbar'
# 预期: 屏幕显示彩条
```

### 10.3 触摸

```shell
adb shell 'cat /proc/bus/input/devices | grep -A2 "dx touch"'
# 预期: Name="dx touch screen"
adb shell 'evtest /dev/input/event4'
# 触摸屏幕: 应见 EV_ABS/ABS_X(0-480)/ABS_Y(0-640)、BTN_TOUCH、SYN_REPORT
```

### 10.4 WiFi(RTL8723DS)

```shell
adb shell 'cat /proc/misc | grep wlan'
# 预期: 200 sunxi-wlan
adb shell 'dmesg | grep -E "RTW: CHIP TYPE|rtw_ndev_init"'
# 预期: RTW: CHIP TYPE: RTL8723D  +  RTW: rtw_ndev_init(wlan0)
adb shell 'ip link set wlan0 up && iw dev wlan0 scan | grep SSID | head -5'
# 预期: 扫到周围 AP

# 连接热点(替换 ssid/psk)
adb shell 'mkdir -p /var/run/wpa_supplicant
cat > /tmp/wpa_test.conf <<EOF
ctrl_interface=/var/run/wpa_supplicant
update_config=1
network={
    ssid="你的热点名"
    psk="你的密码"
    key_mgmt=WPA-PSK
}
EOF
wpa_supplicant -B -i wlan0 -c /tmp/wpa_test.conf'
sleep 8
adb shell 'wpa_cli -i wlan0 status'
# 预期: wpa_state=COMPLETED
adb shell 'udhcpc -i wlan0 -n -q'
# 预期: lease of x.x.x.x obtained
adb shell 'ping -c 3 223.5.5.5'
# 预期: 0% packet loss
```

### 10.5 蓝牙(RTL8723DS H5,A2DP)

```shell
# 开机时序(rc.local 自动): BT 先起,WiFi 后加载
adb shell 'cat /tmp/bt_boot.log'
# 预期: LMP Subversion 0x8723(ROM 干净态)→ IC: RTL8723DS → BT up after 2s → 8723ds loaded (after BT)

# 拉起 A2DP Sink(重启后每次需要)
adb shell '/sbin/start-stop-daemon -b -m -S -p /tmp/bt_test.pid -x /usr/bin/bt_test -- a2dp-sink'
sleep 6
adb shell 'hciconfig hci0'
# 预期: ACL MTU: 1021:8(锁版特征)  UP RUNNING PSCAN ISCAN
adb shell 'hciconfig hci0 -a | grep -E "Name|Class"'
# 预期: Name: 'aw-bt-test-XX-XX'  Class: 0x040400

# 手机操作: 蓝牙搜索 aw-bt-test-XX-XX → 免 PIN 配对 → 媒体音频连接 → 放歌
# 播放时验证:
adb shell 'cat /proc/asound/card0/pcm0p/sub0/status | grep state'
# 预期: state: RUNNING(两次抓取 hw_ptr 持续增长)
adb shell 'hciconfig hci0 | grep RX'
# 预期: RX acl 包持续增长
```

> 蓝牙异常(H5 sync timeout / IC: NONE)时:**拔掉所有线(含 adb USB)整板断电 30 秒**
> 再上电——模块无复位脚,软恢复无效。详见教程"蓝牙排错速查"。

### 10.6 扬声器(LM4871)

```shell
# 主机生成测试音: ffmpeg -f lavfi -i "sine=frequency=440:duration=3" -ar 44100 -ac 1 /tmp/sine.wav
adb push /tmp/sine.wav /tmp/sine.wav
adb shell 'aplay -D hw:audiocodec,0 /tmp/sine.wav'
# 预期: Playing WAVE ... 44100 Hz, Mono, 喇叭出"嘟——"声
# 蓝牙 A2DP 出声(10.5 完成后): 手机放歌,喇叭出声
```

### 10.7 麦克风(GMI4015P,ADC3)

```shell
adb shell 'arecord -d 5 -r 16000 -f S16_LE /tmp/test.wav'
adb shell 'aplay /tmp/test.wav'
# 预期: 能听到自己的声音(近讲建议 ADC3 Gain 15~20,rc.local 已设 19)
```

---

## 第 11 步 获取 docs/ 文档(外部提供)

SDK 的 repo 项目清单里不含 `docs/`,编译**不需要**它。外设教程(`ZGL_BOARD_Tutorial.md`)、
各外设详细记录(`peripheral/01~08`)、原理图、datasheet 都在网盘的 **docs 手册包**里,
下载解压到 SDK 树根目录即可(`<sdk>/docs/...`)。

---

## 遇到问题?

| 情况 | 看哪里 |
|---|---|
| Docker 拉镜像/apt 失败、死代理 | 本手册第 3 步提示框 + `REPLICATION_LOG.md` §1 |
| repo sync 报 Queue/formatter/decode/next 错 | 本手册第 5 步补丁是否 4 处都打上(`python3 /tmp/fix_repo.py` 输出核对) |
| openwrt 编译 `make world` 快速失败,`defconfig`/`.config` 变成 `t113-auto_linux5_10` | 第 6 步 openwrt target 的 `Makefile` 里 `BOARD:=`/`BOARDNAME:=` 没改成 `t113-zgl_board`(改完删 `out/t113/zgl_board/openwrt/.config` 和 `out/.../openwrt/tmp/` 重跑) |
| openwrt 编译报 wifimanager `recursive dependency detected!` 警告 | **可忽略**(SDK 固有的 kconfig 循环依赖,不阻断编译;已实测警告下编译成功) |
| 编译报错 | `README.md` 常见问题表 + `REPLICATION_LOG.md` §4(xxd/busybox 等) |
| 外设不工作 | `ZGL_BOARD_Tutorial.md` 各章"排错速查" + `peripheral/01~07` 详细记录 |
| 从零复刻遇到的坑全集 | `peripheral/08-replication.md` |
