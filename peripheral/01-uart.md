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

### 4.4 adb 验证(同一镜像)

目标板 adbd 已在 rootfs(CONFIG_PACKAGE_adbd=y,rc.local 经 load_script.conf 自启)。

```bash
adb devices       # 应列出设备
adb shell cat /proc/cmdline
```

### 4.5 完成判据

- [ ] 串口看到 BOOT0 → SBOOT → U-Boot → kernel → shell 全链路日志
- [ ] `/proc/cmdline` 含 `console=ttyS3,115200`
- [ ] 之前如果接 PG8/PG9(PG 口的 UART1)能看到过输出,现在应消失(那些脚让给 WiFi/BT 模块)
- [ ] `adb devices` 能列出设备

## 5. 涉及文件

| 文件 | 改动 |
|---|---|
| `device/config/chips/t113/configs/zgl_board/sys_config.fex` | uart_para → PB06/PB07 mux7 |
| `device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts` | uart3_pins_a/b → PB6/PB7;&uart3 okay;&twi3 disabled |
| `device/config/chips/t113/configs/zgl_board/bsp/env.cfg` | earlycon → 0x02500c00 |
| `device/config/chips/t113/configs/zgl_board/buildroot/env.cfg` | 同上 |
| `device/config/chips/t113/configs/zgl_board/openwrt/env.cfg` | 同上(此文件此前已修) |

Patch: `docs/patches/device/0001-t113-add-zgl_board-BSP-with-UART3-debug-console-on-P.patch`
