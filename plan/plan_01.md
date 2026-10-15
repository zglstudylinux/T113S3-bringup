# Plan_01: zgl_board BSP 三阶段调试 — UART → MIPI 屏 → 触摸

> 创建时间: 2026-08-03  
> 目标 BSP: `t113-zgl_board` (基于 Allwinner T113 / Tina5.0 V1.2 SDK)  
> 现状: 镜像可烧即用 (`out/t113_linux_zgl_board_uart0.img`, 21 MB),但**三类核心硬件均未在出厂镜像上启用**,需逐个调通

---

## Context

### 工作流

我负责**写代码 + 改 BSP + 编译新镜像**(用 `script -qec` 走真 pty);你负责**烧录 + 实测 + 反馈**。改完每个阶段都会产生新镜像,你用 PhoenixSuit 烧到板子验证。

### 关键资料(请先读完)

| 文件 | 角色 |
|---|---|
| `docs/sch/SCH_Schematic_new.pdf` | 板子原理图 (最新) |
| `docs/sch/schematic-analysis-1785750417694.md` | 原理图引脚分析 (**权威**) |
| `docs/touch_screen/t113-mipi-touchscreen-evaluation.md` | 屏型号评估报告(D240C2501V1 + ST7701SN + 42 条 init) |
| `docs/touch_screen/D240N2501V1 SPEC.pdf` | 触摸+屏的规格书 |
| `docs/touch_screen/touch_screen_code/dx_touch/dx_touch.c` | 触摸 IC 驱动源码 (350 行成品) |
| `docs/datasheet/T113x_pinout_V1.0.xlsx` + `T113x_Datasheet_V1.4.pdf` | T113 数据手册 |
| `docs/BUILD_T113_ZGL_BOARD.md` | 已有的编译实战报告 |

### 硬件事实(基于 `schematic-analysis-1785750417694.md` 验证)

| 功能 | T113 ball | 复用 / 配置 | 物理连接 | pinctrl driver 验证 |
|---|---|---|---|---|
| **UART3 调试 TX** | **PB6** | mux7 | MX1.25-3P 调试座 U24 pin 2 | `pinctrl-sun8iw20.c:376` (PB6 mux7 = uart3 TX) |
| **UART3 调试 RX** | **PB7** | mux7 | MX1.25-3P 调试座 U24 pin 1 | `pinctrl-sun8iw20.c:388` (PB7 mux7 = uart3 RX) |
| **Touch I2C SCL** | **PE0** | mux4 (TWI1 SCK) | FPC TP_SCL → dx_touch IC | `pinctrl-sun8iw20.c:727` (PE0 mux4 = twi1 SCK) |
| **Touch I2C SDA** | **PE1** | mux4 (TWI1 SDA) | FPC TP_SDA → dx_touch IC | `pinctrl-sun8iw20.c:737` (PE1 mux4 = twi1 SDA) |
| **Touch RST** | **PE4** | gpio_out, active low | FPC TP_RST → dx_touch IC | PE4 mux0/1 = gpio |
| **Touch INT** | **PE5** | gpio_in, falling edge | FPC TP_INT → dx_touch IC | `pinctrl-sun8iw20.c:786` (PE5 IRQ_BANK(0xE, 3, 5)) |
| **DSI Lane0/1** | PD0/PD1, PD2/PD3 | mux4 | FPC2 MIPI_0/1_P/N | D-PHY 标准 |
| **DSI Clock** | PD4/PD5 | mux4 | FPC2 LCD_CLK_P/N | D-PHY 标准 |
| **LCD_RST** | PD20 | gpio_out, active low | FPC2 pin 17 | 标准 GPIO |
| **LCD_TE** | PD21 | gpio_in (可选) | FPC2 pin 18 | 标准 GPIO |
| **LCD_PWM** | PD22 | PWM7 → MT9201 EN | FPC2 LEDA | PWM 输出 |
| **WiFi/BT (不本阶段关注)** | PG0-PG9 | SDIO+UART1 | RTL8723DS 模块 | 不动 |
| **BLE Mesh (不本阶段关注)** | PE2/PE3 | UART0 | MX-02 模块 | 不动 |

**屏型号**: D240C2501V1 (2.4" 480x640 IPS, ST7701SN, **DSI 2-lane, video mode, 21 MHz pclk**)
**触摸 IC**: dx,touch (大显自研, I2C 地址 **0x38**),`docs/touch_screen/touch_screen_code/dx_touch/dx_touch.c` 350 行完整源码已就绪

### 现状 BSP 的核心问题(为什么现在串口不通/屏不亮/触摸不响应)

1. **UART3 引脚配错**: BSP `sys_config.fex [uart_para]` 和 `board.dts [uart3_pins_a]` 把 UART3 配到了 PG8/PG9 mux5,但**原理图上调试串口座 U24 实际接 PB6/PB7**。PG8/PG9 物理上接到 WiFi 模块 UART1_RTS/CTS。意味着 BSP 现在 console 输出送到了 WiFi 模块,不是你的 USB-TTL。
2. **TWI3 占用了即将用作 UART3 的 PB6/PB7**: BSP 的 `&twi3` (status=okay) 把 TWI3 (原用于 AC108 音频 codec @ 0x3B) 配在 PB6/PB7,与新 UART3 引脚直接冲突。`schematic-analysis-1785750417694.md` 没有 AC108 codec 的描述(只有 MIC + LM4871 功放),所以这个 AC108 是 BSP 残留配置,实际板上无此芯片。
3. **触摸 disabled**: `&twi1` (PE0/PE1 当前配 PE14/PE15,需改) 默认 `status="disabled"`,`board.dts` 里的 ctp@14 GT9xx 模板也是 disabled。
4. **MIPI LCD 配错**: `board.dts [lcd0]` 配的是 **LVDS 1024x600 default_lcd**,不是原理图 FPC2 上的 D240C2501V1。

---

## 阶段总览

| 阶段 | 改动量 | 预计编译耗时 | 验证目标 |
|---|---|---|---|
| **A. UART** | 3 文件 (含关闭 TWI3) | bootloader + kernel + pack (~10 min) | 串口全日志链路打通 |
| **B. MIPI 屏** | 6 文件 (3 新增, 3 修改) | kernel + pack (~5 min) | 屏亮 + 显示 logo |
| **C. 触摸** | 4 文件 (1 新增驱动移植, 3 修改) | kernel + pack (~5 min) | evtest 显示坐标 |

---

## 阶段 A: 调通 UART

### A.1 修改文件清单 (3 个)

#### A.1.1 `device/config/chips/t113/configs/zgl_board/sys_config.fex`

**行 188-197** `[uart_para]`:

```diff
 [uart_para]
 uart_debug_port = 3
-uart_debug_tx   = port:PG08<5><1><default><default>
-uart_debug_rx   = port:PG09<5><1><default><default>
+uart_debug_tx   = port:PB06<7><1><default><default>
+uart_debug_rx   = port:PB07<7><1><default><default>
```

mux 值改为 `<7>`(PB6/PB7 mux7 = uart3,pinctrl driver 验证)。

#### A.1.2 `device/config/chips/t113/configs/zgl_board/openwrt/env.cfg`

修两个 bug(都不涉及引脚,只修早期 console 配置):

```diff
 #kernel command arguments
-earlycon=uart8250,mmio32,0x05000000
+earlycon=uart8250,mmio32,0x02500c00
 initcall_debug=0
 console=ttyS3,115200
```

```diff
-setargs_nand=setenv bootargs ... earlyprintk=${earlyprintk} ...
+setargs_nand=setenv bootargs ... earlyprintk=${earlycon} ...
```

`setargs_nand_ubi` 和 `setargs_mmc` 同样替换 `${earlyprintk}` → `${earlycon}`。

#### A.1.3 `device/config/chips/t113/configs/zgl_board/board.dts`

**A. 修改 `uart3_pins_a/b` (行 416-426)**:

```diff
 	uart3_pins_a: uart3_pins@0 {  /* For zgl_board */
-		pins = "PG8", "PG9";
+		pins = "PB6", "PB7";
 		function = "uart3";
 		drive-strength = <10>;
 		bias-pull-up;
 	};
 
 	uart3_pins_b: uart3_pins@1 {  /* For zgl_board */
-		pins = "PG8", "PG9";
+		pins = "PB6", "PB7";
 		function = "gpio_in";
 	};
```

**B. 关闭 `&twi3` (行 907-911)**: 现状是 `status = "okay"` + ac108@3B,占用了 PB6/PB7。直接 disable 整个 &twi3 节点(AC108 芯片实际上不在板上,schematic-analysis 没提)。

```diff
 &twi3 {
     clock-frequency = <400000>;
     pinctrl-0 = <&twi3_pins_a>;
     pinctrl-1 = <&twi3_pins_b>;
     pinctrl-names = "default", "sleep";
-    status = "okay";
+    status = "disabled";
-    ac108: ac108@3B {
-        ...
-    };
+    /* AC108 codec not present on zgl_board; TWI3 disabled to free PB6/PB7 for UART3 */
 };
```

**C. 同时关闭 twi3_pins_b 或保留**: 整个 &twi3 disabled 后 pinctrl-0/1 也无效,无需改 pinctrl 定义本身。`twi3_pins_a` 是 `pins = "PB6", "PB7"`,即使 TWI3 disabled,UART3 仍按 uart3_pins_a 的 PB6/PB7 mux7 重新配置引脚功能。

### A.2 不需要改的文件

- `device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts` 已经被 SoC DTSI 兜底 cmdline (`console=ttyS0`),U-Boot env.cfg 会覆盖为 ttyS3
- `brandy/brandy-2.0/u-boot-2018/configs/sun8iw20p1_auto_defconfig` 的 `CONFIG_CONS_INDEX=4` 选 UART3(1-based 第 4 个 NS16550),pin 选择从 sys_config.fex 读,**不动**
- kernel defconfig 中 `CONFIG_DEBUG_SUNXI_UART3=y` 和 `CONFIG_DEBUG_UART_PHYS=0x02500c00` 已正确(指 UART3 基址,不是具体 pin),**不动**

### A.3 编译命令

sys_config.fex 改动 → 需要重打 bootloader (boot0 读 fex):

```bash
cd /home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2
. tools/scripts/setup-env.sh
source build/envsetup.sh
lunch t113-zgl_board

# bootloader (含 boot0 重打, 5 min)
script -qec "bash -c '. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board && ./build.sh bootloader'" \
  -a ~/t113-zgl-bootloader-A.log

# kernel (board.dts 改动, 3 min)
script -qec "bash -c '. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board && ./build.sh kernel -j8'" \
  -a ~/t113-zgl-kernel-A.log

# pack (40 s)
script -qec "bash -c '. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board && ./build.sh pack'" \
  -a ~/t113-zgl-pack-A.log
```

### A.4 你烧录后的验证

```bash
# 接线: 板子 MX1.25-3P 调试座 (U24) → USB-TTL 3.3V
# U24 pin 1 = UART3_RX (T113 PB7) → USB-TTL TX
# U24 pin 2 = UART3_TX (T113 PB6) → USB-TTL RX
# U24 pin 3 = GND → USB-TTL GND

python3 -m serial.tools.miniterm /dev/ttyUSB0 115200 --parity N --stopbits 1 --bytesize 8
# 或
minicom -D /dev/ttyUSB0 -b 115200  # 关掉硬件/软件流控

# 冷启动, 预期日志顺序:
# 1. "HELLO! BOOT0 is starting!"
# 2. "HELLO! SBOOT is starting!"
# 3. "U-Boot 2018.07-..."
# 4. "Linux version 5.4.61..."            ← earlycon 修好后能看到
# 5. "Please press Enter to activate this console"

# 进 shell 后:
cat /proc/cmdline     # 应含 console=ttyS3,115200
dmesg | grep -E 'ttyS|sunxi-uart'
```

### A.5 完成判据

- 看到 boot0 → U-Boot → kernel → OpenWrt 全链路日志
- `/proc/cmdline` 含 `console=ttyS3,115200`
- **重要验证**: 之前如果接 PG8/PG9 看到过任何输出,现在应该没了(那些信号已让给 WiFi)

---

## 阶段 B: 调通 MIPI 屏 (D240C2501V1 / ST7701SN)

### B.1 新增文件 (3 个)

#### B.1.1 `bsp/drivers/video/sunxi/disp2/disp/lcd/st7701sn.h`

参考模板: `bsp/drivers/video/sunxi/disp2/disp/lcd/inet_dsi_panel.h`

```c
#ifndef __ST7701SN_PANEL_H__
#define __ST7701SN_PANEL_H__
#include "panels.h"
extern struct __lcd_panel st7701sn_panel;
#endif
```

#### B.1.2 `bsp/drivers/video/sunxi/disp2/disp/lcd/st7701sn.c`

参考模板: `bsp/drivers/video/sunxi/disp2/disp/lcd/inet_dsi_panel.c`

**42 条 ST7701SN init DCS 命令** 直接从 `docs/touch_screen/t113-mipi-touchscreen-evaluation.md` §6.2 (第 296-310 行起的 `st7701sn_init_cmds[]` 数组) 复制。该报告已从 RK 参考代码提取并转换为 T113 disp2 格式。

**关键结构** (参考 inet_dsi_panel.c 的同名函数实现):

```c
static void LCD_power_on(u32 sel);     // GPIO 复位 PD20 active low
static void LCD_power_off(u32 sel);
static void LCD_bl_open(u32 sel);      // PD22 PWM7
static void LCD_bl_close(u32 sel);
static void LCD_panel_init(u32 sel);   // 42 条 DSI DCS 命令
static void LCD_panel_exit(u32 sel);   // 0x28 + 0x10

static void LCD_cfg_panel_info(panel_extend_para *info);
static void LCD_cfg_dsi(panel_dsi_para *dsi);

static s32 LCD_open_flow(u32 sel);
static s32 LCD_close_flow(u32 sel);

struct __lcd_panel st7701sn_panel = {
    .name           = "st7701sn",
    .lcd_if         = 4,           /* DSI */
    .lcd_dsi_if     = 0,           /* Video mode burst */
    .lcd_x          = 480,
    .lcd_y          = 640,
    .lcd_dclk_freq  = 21,          /* MHz, RK 参考 */
    .lcd_ht         = 502,         /* 480+10+2+10 */
    .lcd_hbp        = 10,
    .lcd_hspw       = 2,
    .lcd_vt         = 702,         /* 640+30+2+30 */
    .lcd_vbp        = 30,
    .lcd_vspw       = 2,
    .lcd_dsi_lane   = 2,           /* D240C2501V1 是 2-lane */
    .lcd_dsi_format = 0,           /* RGB888 */
    .lcd_dsi_te     = 0,
    .lcd_gpio_0     = "PD20",      /* LCD_RST active low */
    .lcd_bl_en      = "PD22",      /* Backlight PWM7 */
    .lcd_bl_pwm_value = 200,
    .lcd_pwm_ch     = 7,
    .lcd_power      = "vcc-lcd",

    .open_flow      = LCD_open_flow,
    .close_flow     = LCD_close_flow,
    .panel_init     = LCD_panel_init,
    .panel_exit     = LCD_panel_exit,
};
```

**DSI pinctrl**: `dsi2lane_pins_a/b` 引用自 `kernel/linux-5.4/arch/arm/boot/dts/sun8iw20p1.dtsi` (SoC DTSI 定义)。若命名不同,需要 board.dts 自己声明 PD0-PD5 mux4 的 pinmux group。

#### B.1.3 (可选) `device/config/chips/t113/configs/zgl_board/bootlogo_480x640.bmp`

当前 `bootlogo.bmp` 是 800x480,与 480x640 不匹配,boot_gui 自动缩放后会糊。如果你提供 480x640 的 logo 我替换;否则保留原 logo 接受拉伸。

### B.2 修改文件清单 (4 个)

#### B.2.1 `bsp/drivers/video/sunxi/disp2/disp/lcd/panels.c`

在 `panel_array[]` 加注册行:

```c
#if IS_ENABLED(CONFIG_LCD_SUPPORT_ST7701SN)
    &st7701sn_panel,
#endif
```

#### B.2.2 `bsp/drivers/video/sunxi/disp2/disp/lcd/Kconfig`

```
config LCD_SUPPORT_ST7701SN
    bool "support st7701sn panel (D240C2501V1)"
    depends on DISP2_SUNXI
    default y
    help
      ST7701SN 2-lane DSI 480x640, used by D240C2501V1 2.4" IPS panel.
```

#### B.2.3 `bsp/drivers/video/sunxi/disp2/disp/lcd/Makefile`

```
obj-$(CONFIG_LCD_SUPPORT_ST7701SN) += st7701sn.o
```

#### B.2.4 `device/config/chips/t113/configs/zgl_board/board.dts`

修改 `&lcd0` 节点 (行 1385-1427) 切到 DSI:

```diff
 &lcd0 {
     lcd_used               = <1>;
-    lcd_driver_name        = "default_lcd";
-    lcd_if                 = <3>;            /* LVDS */
-    lcd_x                  = <1024>;
-    lcd_y                  = <600>;
-    lcd_dclk_freq          = <51>;
-    lcd_ht                 = <1344>;
-    lcd_hbp                = <100>;
-    lcd_hspw               = <10>;
-    lcd_vt                 = <635>;
-    lcd_vbp                = <20>;
-    lcd_vspw               = <5>;
+    lcd_driver_name        = "st7701sn";
+    lcd_if                 = <4>;            /* DSI */
+    lcd_dsi_if             = <0>;            /* Video mode burst */
+    lcd_x                  = <480>;
+    lcd_y                  = <640>;
+    lcd_dclk_freq          = <21>;
+    lcd_ht                 = <502>;
+    lcd_hbp                = <10>;
+    lcd_hspw               = <2>;
+    lcd_vt                 = <702>;
+    lcd_vbp                = <30>;
+    lcd_vspw               = <2>;
+    lcd_dsi_lane           = <2>;
+    lcd_dsi_format         = <0>;            /* RGB888 */
+    lcd_dsi_te             = <0>;
+    lcd_gpio_0             = "PD20";
+    lcd_bl_en              = "PD22";
+    lcd_bl_pwm_value       = <200>;
+    lcd_pwm_ch             = <7>;
+    lcd_power              = "vcc-lcd";
     pinctrl-0 = <&dsi2lane_pins_a>;
     pinctrl-1 = <&dsi2lane_pins_b>;
     status = "okay";
 };
```

#### B.2.5 (可选) `device/config/chips/t113/configs/zgl_board/uboot-board.dts`

U-Boot 阶段 logo 显示需要单独配置,字段同上。不改亦可 (boot0/U-Boot 阶段黑屏不影响 kernel 阶段 framebuffer)。

### B.3 不需要改的文件

- `device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4`: `CONFIG_DISP2_SUNXI=y` 已开,**CONFIG 不用动**

### B.4 编译命令

```bash
. tools/scripts/setup-env.sh
source build/envsetup.sh
lunch t113-zgl_board

script -qec "bash -c '. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board && ./build.sh kernel -j8'" \
  -a ~/t113-zgl-kernel-B.log
script -qec "bash -c '. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board && ./build.sh pack'" \
  -a ~/t113-zgl-pack-B.log
```

预计: kernel ~5 min, pack ~40 s。

### B.5 你烧录后的验证

```bash
dmesg | grep -iE 'st7701sn|panel|lcd0|dsi|disp'
# 预期: "sunxi lcd0 panel: st7701sn", "DE2.0 init ok", "DSI 2-lane"

ls /dev/fb* /dev/disp
# 预期: /dev/fb0

echo 1 > /sys/class/disp/disp/attr/enable
cat /dev/urandom > /dev/fb0 &   # 雪花屏验证 framebuffer
```

### B.6 完成判据

- 屏背光亮 (PD22 PWM7)
- 屏上有图像 (即使颜色不对)
- `/dev/fb0` 可读写

### B.7 失败 fallback

| 症状 | 调试 |
|---|---|
| 屏不亮 | `grep st7701sn out/.../boot.img` (lzma 解开看是否编入) |
| 亮但全黑 | 示波器量 PD0-PD5 DSI 信号; 检查 42 条 DCS 命令 |
| 颜色乱 | 改 `lcd_dsi_format` 0↔1↔2 |
| 只显示一半 | 调 hfp/hspw/hbp/vfp/vspw/vbp |
| 方向反 | init 序列最后加 0x36 (MADCTL) |

---

## 阶段 C: 调通触摸 (dx_touch @ I2C 0x38)

**触摸 IC = dx,touch, I2C 地址 0x38, 接 TWI1 (PE0/PE1), RST=PE4, INT=PE5**

dx_touch.c 源码无需修改,直接复制 + 设备树配置。

### C.1 新增/移植文件 (1 个源 + 1 个 Kconfig/Makefile 改动)

#### C.1.1 移植 `dx_touch.c` 到 SDK

源: `docs/touch_screen/touch_screen_code/dx_touch/dx_touch.c` (350 行成品)
目标: `kernel/linux-5.4/drivers/input/touchscreen/dx_touch/dx_touch.c`

```bash
mkdir -p kernel/linux-5.4/drivers/input/touchscreen/dx_touch
cp docs/touch_screen/touch_screen_code/dx_touch/dx_touch.c \
   kernel/linux-5.4/drivers/input/touchscreen/dx_touch/
```

#### C.1.2 新建 `kernel/linux-5.4/drivers/input/touchscreen/dx_touch/Makefile`

```
obj-y += dx_touch.o
```

#### C.1.3 修改 `kernel/linux-5.4/drivers/input/touchscreen/Kconfig`

```
config TOUCHSCREEN_DX_TOUCH
    tristate "DX Touchscreen (D240C2501V1)"
    depends on I2C && INPUT
    help
      Say Y here to enable support for the DX touchscreen controller
      used on D240C2501V1 2.4" MIPI panel. I2C address is 0x38.
```

#### C.1.4 修改 `kernel/linux-5.4/drivers/input/touchscreen/Makefile`

```
obj-$(CONFIG_TOUCHSCREEN_DX_TOUCH) += dx_touch/
```

### C.2 修改文件清单 (3 个)

#### C.2.1 `device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4`

```
+CONFIG_TOUCHSCREEN_DX_TOUCH=y
 CONFIG_TOUCHSCREEN_GT9XXNEW_TS=y   (保留或注释掉,二者不冲突)
```

#### C.2.2 `device/config/chips/t113/configs/zgl_board/board.dts`

**A. 修改 `twi1_pins_a/b` (行 439-448)** — 把 PE14/PE15 改成 PE0/PE1:

```diff
 	twi1_pins_a: twi1@0 {
-		pins = "PE14", "PE15";
+		pins = "PE0", "PE1";
 		function = "twi1";
 		drive-strength = <10>;
 	};
 
 	twi1_pins_b: twi1@1 {
-		pins = "PE14", "PE15";
+		pins = "PE0", "PE1";
 		function = "gpio_in";
 	};
```

**B. 启用 `&twi1` (行 888-895),加 dxts@38 节点**:

```diff
 &twi1 {
     clock-frequency = <400000>;
     pinctrl-0 = <&twi1_pins_a>;
     pinctrl-1 = <&twi1_pins_b>;
     pinctrl-names = "default", "sleep";
-    status = "disabled";
+    status = "okay";
+
+    dxts@38 {
+        compatible    = "dx,touch";
+        reg           = <0x38>;
+        max-x         = <480>;
+        max-y         = <640>;
+        tp-size       = <89>;
+        interrupt-parent = <&pio>;
+        /* PE5 = port 5 (PE), pin 5 (PE5), falling edge */
+        interrupts    = <5 5 IRQ_TYPE_EDGE_FALLING>;
+        /* PE4 = port 5 (PE), pin 4 (PE4), active low */
+        reset-gpios   = <&pio 5 4 GPIO_ACTIVE_LOW>;
+        wakeup-source;
+        status        = "okay";
+    };
 };
```

**C. (可选) 关闭 GT9xx 模板**: `&twi0` 中的 ctp@14 子节点(行 867-885) `status="disabled"` 是默认,不动也行。

### C.3 不需要改的文件

- `&pio` 节点 — PE4/PE5 已是 GPIO,无需新声明
- pinctrl group 名 `twi1_pins_a/b` 已在 board.dts 定义,无需在 DTSI 新加

### C.4 编译命令

```bash
. tools/scripts/setup-env.sh
source build/envsetup.sh
lunch t113-zgl_board

script -qec "bash -c '. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board && ./build.sh kernel -j8'" \
  -a ~/t113-zgl-kernel-C.log
script -qec "bash -c '. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board && ./build.sh pack'" \
  -a ~/t113-zgl-pack-C.log
```

预计: kernel ~5 min, pack ~40 s。

### C.5 你烧录后的验证

```bash
dmesg | grep -iE 'dx_touch|dx,touch|twi1|i2c.*0x38|input'
# 预期: "dx_touch 0-0038: probed", "input: dx_touch as /devices/.../input/input0"

opkg update && opkg install evtest i2c-tools

i2cdetect -y 1          # TWI1 是 bus 1
# 预期: 0x38 行显示 "38" 或 "UU"

ls /dev/input/
# 预期: event0

evtest /dev/input/event0
# 触摸屏幕, 看到 ABS_MT_POSITION_X/Y 事件, 坐标 0..479 / 0..639
```

### C.6 完成判据

- `i2cdetect -y 1` 看到 0x38
- `evtest` 触摸产生事件
- 坐标范围 0..479 / 0..639

### C.7 失败 fallback

| 症状 | 调试 |
|---|---|
| i2cdetect 看不到 0x38 | 量 PE0/PE1 波形; 试 0x14 (GT911 备用) |
| i2cdetect 看到但 evtest 无事件 | 量 PE5 是否有下降沿; 试 `IRQ_TYPE_LEVEL_LOW` |
| 触摸 X 反 | 注释 `dx_touch.c:386` 的 `input_x = 480 - ...` |
| 触摸 Y 反 | 翻转 Y 计算 |
| 触摸点和显示对不上 | 在 ST7701SN init 序列加 0x36 (MADCTL) |

---

## 阶段间依赖

- **A → B**: A 修好 UART 后,B 阶段的内核 log 能完整看到(定位 DSI 问题更方便)
- **B → C**: C 独立,但 B 完成后有图像才好调触摸方向

---

## 总耗时预估

| 阶段 | 写代码 | 编译 | 烧录 + 调试 | 一轮往返 |
|---|---|---|---|---|
| A. UART | ~5 min | ~10 min (bootloader + kernel + pack) | ~15 min | ~30 min |
| B. MIPI | ~30-60 min (主要写 st7701sn.c 42 条 init) | ~5 min | ~30 min | 1.5-2 h |
| C. Touch | ~10 min | ~5 min | ~15 min | ~30 min |

总计: **~3 小时**

---

## 风险与备选

| 风险 | 严重度 | 备选 |
|---|---|---|
| A: 改 sys_config.fex 后 boot0 仍按 PG8/PG9 输出 | 中 | `cd brandy/brandy-2.0 && make clean && make` 全清重打 |
| A: AC108 关闭后音频无 | 低 | 接受;后续若需恢复,需找 AC108 实际焊的引脚对 |
| B: 42 条 DCS init 仍不能点亮屏 | 中 | 改用 DRM 模式 (`CONFIG_DRM_SUN4I=y`), 更多 CONFIG 改动 |
| B: `dsi2lane_pins_a` 命名问题 | 中 | 在 board.dts 显式声明 pinmux group |
| C: dx_touch 硬编码 480-X 翻转不能改 | 低 | 改源码,把 480 改为 max_x 动态值 |
| C: I2C 地址 0x38 实际是 0x14 | 低 | 试 i2cdetect 所有地址 |

---

## 文件改动汇总表

| 阶段 | 文件 | 类型 | 改动量 |
|---|---|---|---|
| A | `device/config/chips/t113/configs/zgl_board/sys_config.fex` | modify | 改 uart_debug_tx/rx 引脚 (PB06/PB07<7>) |
| A | `device/config/chips/t113/configs/zgl_board/openwrt/env.cfg` | modify | earlycon 地址 + 3 处 ${earlyprintk} → ${earlycon} |
| A | `device/config/chips/t113/configs/zgl_board/board.dts` | modify | uart3_pins_a/b 改 PB6/PB7; &twi3 status="disabled" (含 ac108@3B 注释) |
| B | `bsp/drivers/video/sunxi/disp2/disp/lcd/st7701sn.h` | **new** | ~10 行 |
| B | `bsp/drivers/video/sunxi/disp2/disp/lcd/st7701sn.c` | **new** | ~300 行 (含 42 条 init) |
| B | `bsp/drivers/video/sunxi/disp2/disp/lcd/panels.c` | modify | 注册 st7701sn (4 行) |
| B | `bsp/drivers/video/sunxi/disp2/disp/lcd/Kconfig` | modify | 新增 config (7 行) |
| B | `bsp/drivers/video/sunxi/disp2/disp/lcd/Makefile` | modify | obj-y (1 行) |
| B | `device/config/chips/t113/configs/zgl_board/board.dts` | modify | &lcd0 改 DSI (~30 行) |
| B (可选) | `device/config/chips/t113/configs/zgl_board/uboot-board.dts` | modify | U-Boot 阶段 lcd0 (~30 行) |
| C | `kernel/linux-5.4/drivers/input/touchscreen/dx_touch/dx_touch.c` | **new** (移植) | 350 行 |
| C | `kernel/linux-5.4/drivers/input/touchscreen/dx_touch/Makefile` | **new** | 1 行 |
| C | `kernel/linux-5.4/drivers/input/touchscreen/Kconfig` | modify | 新增 config (8 行) |
| C | `kernel/linux-5.4/drivers/input/touchscreen/Makefile` | modify | obj-y (1 行) |
| C | `device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4` | modify | 开 CONFIG_TOUCHSCREEN_DX_TOUCH=y |
| C | `device/config/chips/t113/configs/zgl_board/board.dts` | modify | twi1_pins 改 PE0/PE1; &twi1 加 dxts@38 (~20 行) |

**总计**: **3 个新文件 + 8 个修改文件**,改动行数 ~750 行。

---

## 关键约束(写代码时不能违反)

1. **UART3 引脚必须是 PB6/PB7 mux7** — 物理不可改
2. **TWI3 必须禁用或搬到 PB6/PB7 之外** — 与 UART3 直接冲突
3. **MIPI 引脚 PD0-PD5 + PD20/PD21/PD22** — 物理不可改
4. **Touch I2C 必须用 TWI1 (PE0/PE1)** — 物理不可改
5. **Touch RST = PE4, INT = PE5** — 物理不可改
6. **AC108 音频 codec 暂时放弃** — 引脚让给了 UART3

---

## 你需要提供的资料

1. **A**: 不需要
2. **B**: 如果有更精确的 ST7701SN 时序参数(从屏厂),提供一下;否则用 eval 报告 §6.2 的 RK 参考值
3. **C**: 不需要 (dx_touch.c 源码已现成)
