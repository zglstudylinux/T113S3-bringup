# 01 — 调试串口 UART3(PB6/PB7)

> 状态: **✅ 已调通**(2026-10-15, 镜像 `out/t113_linux_zgl_board_uart0.img`, md5 c541e473…)  
> 平台: T113-S3 / Tina 5.0 V1.2 / Linux 5.4.61 / `t113-zgl_board`

---

## 1. 原理图

调试串口走 **UART3**,接到板上的 3P 调试座 U24(ZX-MX1.25-3PWT):

| U24 引脚 | T113 引脚 | 网络名 | 方向 | 说明 |
|---|---|---|---|---|
| Pin 1 | **PB7** | UART3_RX | 输入 | 接 USB-TTL 的 TX |
| Pin 2 | **PB6** | UART3_TX | 输出 | 接 USB-TTL 的 RX |
| Pin 3 | GND | — | — | 共地 |

- 电平 3.3V TTL,115200 8N1,无流控
- U37/U38(ESD5451N)对地保护
- PB6/PB7 的 mux7 = UART3(`pinctrl-sun8iw20.c:376/388`)
- **冲突让路**: PB6/PB7 原被 `twi3`(AC108 codec,板上无此芯片)、`wlan_regon`、`bt_rst_n` 引用,全部已禁用/移除

原理图截图见 `docs/sch/T113.pdf` 页 4(CONNECT)。

## 2. 修改内容

### 2.1 `device/config/chips/t113/configs/zgl_board/sys_config.fex`

Boot0/SBoot 阶段的调试串口配置(uart_para 段):

```fex
[uart_para]
uart_debug_port = 3
uart_debug_tx   = port:PB06<7><1><default><default>
uart_debug_rx   = port:PB07<7><1><default><default>
```

`<7>` = mux7(UART3 功能)。此文件由 boot0 在打包时读取,改后需重新 `./build.sh pack`。

### 2.2 `device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts`

内核阶段引脚复用(pinctrl 节点):

```dts
uart3_pins_a: uart3_pins@0 {  /* For zgl_board, debug UART3 on PB6/PB7 */
	pins = "PB6", "PB7";
	function = "uart3";
	drive-strength = <10>;
	bias-pull-up;
};

uart3_pins_b: uart3_pins@1 {  /* For zgl_board */
	pins = "PB6", "PB7";
	function = "gpio_in";
};
```

UART3 控制器节点启用:

```dts
&uart3 {
	pinctrl-names = "default", "sleep";
	pinctrl-0 = <&uart3_pins_a>;
	pinctrl-1 = <&uart3_pins_b>;
	status = "okay";
};
```

TWI3 禁用(给 UART3 让出 PB6/PB7;AC108 codec 不在板上):

```dts
&twi3 {
	clock-frequency = <400000>;
	pinctrl-0 = <&twi3_pins_a>;
	pinctrl-1 = <&twi3_pins_b>;
	pinctrl-names = "default", "sleep";
	status = "disabled";
};

/* TWI3 disabled to free PB6/PB7 for UART3 debug serial.
 * AC108 audio codec not present on zgl_board.
 */
```

### 2.3 三套 `env.cfg` 的 earlycon 修正

`bsp/env.cfg`、`buildroot/env.cfg`、`openwrt/env.cfg`:

```diff
-earlycon=uart8250,mmio32,0x05000000
+earlycon=uart8250,mmio32,0x02500c00
 console=ttyS3,115200
```

`0x02500c00` 是 **UART3 的基址**(0x05000000 是 UART0 的,之前 earlycon 打到了错误串口)。
`openwrt/env.cfg` 的 setargs 里 `earlyprintk=${earlycon}` 已引用此变量,无需改。

### 2.4 不需要改的文件

- `brandy/brandy-2.0/u-boot-2018/configs/sun8iw20p1_auto_defconfig`:`CONFIG_CONS_INDEX=4`(1-based 第 4 个 NS16550 = UART3),pin 从 sys_config.fex 读
- `linux-5.4/config-5.4`:`CONFIG_DEBUG_SUNXI_UART3=y`、`CONFIG_DEBUG_UART_PHYS=0x02500c00` 已正确

## 3. 重新编译打包

```bash
cd /home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2
. tools/scripts/setup-env.sh
source build/envsetup.sh
lunch t113-zgl_board

./build.sh kernel -j8   # board.dts → dtb → boot.img (~1 min 增量)
./build.sh pack         # sys_config.fex + boot.img → 镜像 (~5 min)
# 产物: out/t113_linux_zgl_board_uart0.img
```

## 4. 测试与结果

### 4.1 硬件接线

```
板子 U24 调试座(3P)        USB-TTL(3.3V)
Pin 1  UART3_RX (PB7)  ←→  TX
Pin 2  UART3_TX (PB6)  ←→  RX
Pin 3  GND             ←→  GND
```

### 4.2 串口验证

```bash
minicom -D /dev/ttyUSB0 -b 115200   # 关掉硬件/软件流控
# 或
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```

冷启动预期日志顺序:

```
1. HELLO! BOOT0 is starting!
2. HELLO! SBOOT is starting!
3. U-Boot 2018.07-...
4. Linux version 5.4.61-...      ← earlycon 修好后可见
5. Please press Enter to activate this console
```

### 4.3 系统内验证

```bash
cat /proc/cmdline          # 应含 console=ttyS3,115200 earlycon=uart8250,mmio32,0x02500c00
dmesg | grep -E 'ttyS|sunxi-uart'   # 应见 uart3 注册(0x02500c00)
```

### 4.4 adb 验证(同一镜像, 2026-10-15 实测)

目标板 adbd 已在 rootfs(CONFIG_PACKAGE_adbd=y,rc.local 经 load_script.conf 自启)。

主机端准备(VMware 虚拟机需先把板子 USB 接进虚拟机;宿主是 Linux 时装 udev 规则):

```bash
# /etc/udev/rules.d/51-android.rules (Tina ADB 的 VID=1f3a; 标准 adb=18d1)
SUBSYSTEM=="usb", ATTR{idVendor}=="1f3a", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", MODE="0666", GROUP="plugdev"
# 然后: sudo udevadm control --reload && sudo udevadm trigger && adb kill-server
```

实测(`adb devices` → `20080411  device`):

```text
$ adb shell cat /proc/cmdline
earlyprintk=uart8250,mmio32,0x02500c00 clk_ignore_unused initcall_debug=0 \
console=ttyS3,115200 loglevel=8 root=/dev/mmcblk0p5 init=/init rdinit=/rdinit \
partitions=boot-resource@mmcblk0p1:env@mmcblk0p2:env-redund@mmcblk0p3:boot@mmcblk0p4:\
rootfs@mmcblk0p5:rootfs_data@mmcblk0p6:private@mmcblk0p7:UDISK@mmcblk0p8 \
cma=4M ... androidboot.hardware=sun8iw20p1 boot_type=1 ... \
uboot_message=2018.07-gce06dac(03/15/2025-06:33:14) ... androidboot.dramsize=128

$ adb shell uname -a
Linux TinaLinux 5.4.61 #4 SMP PREEMPT Thu Oct 15 20:09:31 CST 2026 armv7l GNU/Linux
$ adb shell cat /etc/openwrt_release
DISTRIB_ID='OpenWrt'
DISTRIB_RELEASE='21.02-SNAPSHOT'
```

要点:
- `console=ttyS3,115200` 与 `earlycon 0x02500c00`(UART3 基址)均在 cmdline 中 ✓
- 烧录方式: **TF 卡**(全志 TF 卡烧写工具)。注意 `boot_type=1`(SD 卡启动);此前用 PhoenixSuit 烧写时因介质不符出现 `spinand read id failed` 复位,属烧写工具/介质选择问题,镜像本身无误

### 4.5 烧写工具选择(踩坑记录)

| 工具 | 适用 |
|---|---|
| **全志 TF 卡烧写工具**(PhoenixCard 类) | TF 卡启动(本板当前方式, `boot_type=1`)✓ |
| PhoenixSuit | 烧写 spi-nand/eMMC 整机镜像(`boot_type=0`) |

用 PhoenixSuit 烧出的镜像写到 TF 卡时,boot0 按错误介质初始化,启动中段报
`MMC init failed → try emmc fail → sunxi-spinand read id failed : -110 → try nand fail → initcall sequence failed → RESET`。
**TF 卡启动务必用 TF 卡烧写工具**。

### 4.6 完成判据(2026-10-15 实测全部通过)

- [x] 串口看到 BOOT0 → SBOOT → U-Boot → kernel → shell 全链路日志(用户实测)
- [x] `/proc/cmdline` 含 `console=ttyS3,115200` + earlycon 0x02500c00(adb 实测)
- [x] PG8/PG9 无调试输出(那些脚让给 WiFi/BT 模块)
- [x] `adb devices` 列出设备 `20080411`

## 5. 涉及文件

| 文件 | 改动 |
|---|---|
| `device/config/chips/t113/configs/zgl_board/sys_config.fex` | uart_para → PB06/PB07 mux7 |
| `device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts` | uart3_pins_a/b → PB6/PB7;&uart3 okay;&twi3 disabled |
| `device/config/chips/t113/configs/zgl_board/bsp/env.cfg` | earlycon → 0x02500c00 |
| `device/config/chips/t113/configs/zgl_board/buildroot/env.cfg` | 同上 |
| `device/config/chips/t113/configs/zgl_board/openwrt/env.cfg` | 同上(此文件此前已修) |

