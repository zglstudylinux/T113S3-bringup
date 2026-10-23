# 全志 T113-S3 ZGL_BOARD 从零适配外设教程

> 目标板:自定义 T113-S3 板卡(板级名 `zgl_board`),Tina 5.0 SDK
> 适配范围:调试串口(UART3)→ MIPI 屏(D240N2501V1/ST7701SN)→ 触摸(dx_touch)→ WiFi(RTL8723DS)→ 蓝牙(RTL8723DS H5)→ 扬声器(LM4871)→ 麦克风(ADC3)
> 本教程按顺序从零复刻,每一步都给出修改哪个文件、修改什么内容;初学者照着做即可。

## 硬件资源总览

对照原理图,本板的外设引脚分配如下(后续各章都会用到):

| 外设 | 器件 | 引脚 |
|---|---|---|
| 调试串口 | UART3 → U24 3P 调试座 | PB6(TX)/PB7(RX),115200 |
| MIPI 屏 | D240N2501V1(ST7701SN,2.4" 480x640 IPS) | 2-lane DSI:PD0~PD5;复位 PD20;背光 PWM7=PD22 |
| 触摸 | I2C 0x38(dx_touch) | SCL=PE0/SDA=PE1;复位 PE4;中断 PE5 |
| WiFi | RTL8723DS(SDIO) | SDIO D0-D3/CMD/CLK:PG0~PG5 |
| 蓝牙 | RTL8723DS(UART H5) | UART1 四线:PG6(TX)/PG7(RX)/PG8(RTS)/PG9(CTS) |
| 扬声器 | LM4871 功放 | PA_SHDN=PB2(硬件下拉,常使能);HPOUTR=Pin98 |
| 麦克风 | GMI4015P 差分模拟麦 | MICIN3P=Pin87/MICIN3N=Pin88(codec ADC3 通道) |

<!-- 截图: 板卡实物图 -->

## 环境准备

### 主机环境

主机需要 x86_64 Linux(示例用 Ubuntu),安装编译依赖:

> **推荐:用 Docker 跳过宿主环境差异**(可选)。不同 Ubuntu 版本的报错根源有 4 类
> (32 位 pack 工具、Python 版本窗口、宿主 gcc/make 版本、repo 工具),统一用
> Ubuntu 22.04 容器可全部规避,且已实测从解压 tar 到产出镜像全流程一致。
> 见 `docs/build-env/docker/README.md`(两条命令:`build-image` + `zgl_build`),
> 踩坑实录见 `docs/build-env/docker/REPLICATION_LOG.md`。下面仍然给出宿主直编的依赖。

```shell
sudo apt install build-essential flex bison libncurses-dev libssl-dev libelf-dev \
  zlib1g-dev device-tree-compiler gawk autoconf automake libtool pkg-config \
  subversion cpio curl lz4 help2man texinfo gettext xz-utils python3-dev \
  time cmake libc6-i386 lib32stdc++6
```

> 注意:`libc6-i386`/`lib32stdc++6` 是 32 位库,全志的 `fsbuild` 打包工具需要;`subversion` 是部分软件包源码拉取需要。

### 下载和解压 SDK

解压:

```shell
tar -xvf T113-Tina5.0-V1.2.tar
```

解压成功后先只有 `.repo` 隐藏目录,拉取代码:

```shell
.repo/repo/repo sync -l
```

> 环境坑(2026-10 复刻实录,详见 `docs/peripheral/08-replication.md`):
> 1. 主机只有 Python 3.10+ 时,捆绑的 repo 1.24 会报 `No module named 'imp'`——
>    需要 imp 兼容垫片 + py3 启动器(08-replication.md §1.1 有完整可复制的修法);
> 2. 首次编译会弹免责声明等输入,后台/脚本环境会卡死——预置
>    `touch <sdk>/.disclaimer_accpet`(或拷参考树同名文件)跳过;
> 3. `tools/scripts/`、`docs/` 不在 repo 工程清单里,若解压后缺失需从原发布包补。

<!-- 截图: repo sync 完成 -->

拉取完成后 `ls` 应看到 `build/ device/ kernel/ brandy/ openwrt/ platform/ docs/` 等目录。

### 创建和配置板级支持包

复制一份 nand 方案并重命名(看原理图得知板卡存储是 SPI NAND + TF 卡):

```shell
cd <sdk>/device/config/chips/t113/configs
cp -r evb1_auto_nand zgl_board
```

修改 `zgl_board/sys_config.fex`:

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/sys_config.fex
```

修改内容 —— diff(原值 → 新值,看差异):

```diff
 [product]
 version = "100"
-machine = "evb1"
+machine = "zgl_board"

 ;storage_type = boot medium, 0-nand, 1-sd, 2-emmc, 3-nor, 4-emmc3, 5-spinand
 storage_type    = 5            (保持 5:SPI NAND;TF 卡启动由烧写工具的 boot_type 控制,见烧录章节)
```

修改后 `[product]` 段的最终内容(直接复制):

```shell
[product]
version = "100"
machine = "zgl_board"
```

### 添加 openwrt 文件系统对自定义板卡的支持方案

创建板级支持包后,添加 openwrt 目标方案(方案名与上一步的 machine 名对应):

```shell
cd <sdk>/openwrt/target/t113
cp -r t113-evb1_auto_nand t113-zgl_board
```

修改 `t113-zgl_board/vendorsetup.sh`(注册 lunch 选项):

```shell
vi <sdk>/openwrt/target/t113/t113-zgl_board/vendorsetup.sh
```

修改后的最终内容(直接复制,文件末尾一行):

```shell
add_lunch_combo t113-zgl_board
```

修改 `t113-zgl_board/t113_zgl_board.mk`(产品名,原文件名是 t113_evb1_auto_nand.mk,先改名):

```shell
cd <sdk>/openwrt/target/t113/t113-zgl_board
mv t113_evb1_auto_nand.mk t113_zgl_board.mk
vi t113_zgl_board.mk
```

修改后的最终内容(直接复制,整体替换):

```shell
$(call inherit-product-if-exists, target/allwinner/t113-common/t113-common.mk)

PRODUCT_PACKAGES +=

PRODUCT_COPY_FILES +=

PRODUCT_AAPT_CONFIG := large xlarge hdpi xhdpi
PRODUCT_AAPT_PERF_CONFIG := xhdpi
PRODUCT_CHARACTERISTICS := musicbox

PRODUCT_BRAND := allwinner
PRODUCT_NAME := t113_zgl_board
PRODUCT_DEVICE := t113-zgl_board
PRODUCT_MODEL := Allwinner t113 zgl_board board
```

修改 `t113-zgl_board/TinaProducts.mk`(产品 makefile 入口):

```shell
vi <sdk>/openwrt/target/t113/t113-zgl_board/TinaProducts.mk
```

修改后的最终内容(直接复制,替换 `PRODUCT_MAKEFILES` 一行):

```shell
PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/t113_zgl_board.mk
```

修改 `t113-zgl_board/Makefile`(目标板名,**必改**——漏了它 openwrt 扫描认不出
zgl_board 目标,menuconfig 时 .config 会被重置成默认板,编译直接失败):

```shell
vi <sdk>/openwrt/target/t113/t113-zgl_board/Makefile
```

修改内容 —— diff(原值 → 新值,看差异):

```diff
 ARCH:=arm
-BOARD:=t113-evb1_auto_nand
-BOARDNAME:=t113-evb1_auto_nand
+BOARD:=t113-zgl_board
+BOARDNAME:=t113-zgl_board
 FEATURES:=fpu dt
 CPU_TYPE:=cortex-a7
 CPU_SUBTYPE:=neon
 MAINTAINER:=Allwinner
```

修改后 `BOARD`/`BOARDNAME` 两行的最终内容(直接复制,替换对应两行):

```shell
BOARD:=t113-zgl_board
BOARDNAME:=t113-zgl_board
```

> 说明:`BOARD` 是 openwrt 目标标识(targetinfo 扫描、defconfig 一致性检查都用它),
> 必须与目录名 `t113-zgl_board` 和 defconfig 的 `CONFIG_TARGET_BOARD` 一致;
> 其余行(ARCH/FEATURES/CPU_TYPE/KERNEL_PATCHVER 等)保持模板原样不用动。

修改 `t113-zgl_board/defconfig`(目标名):

```shell
vi <sdk>/openwrt/target/t113/t113-zgl_board/defconfig
```

修改内容 —— diff(替换目标相关行;其余 6900+ 行保持模板原样,后续外设章再改需要的包):

```diff
-CONFIG_TARGET_t113_evb1_auto_nand=y
-CONFIG_TARGET_t113_evb1_auto_nand_Default=y
-CONFIG_TARGET_BOARD="t113-evb1_auto_nand"
+CONFIG_TARGET_t113_zgl_board=y
+CONFIG_TARGET_t113_zgl_board_Default=y
+CONFIG_TARGET_BOARD="t113-zgl_board"
```

> 说明:`CONFIG_PACKAGE_adbd=y`(adb 用)模板已开,不用改。
> 其余外设需要的包(evtest/i2c-tools/rtl8723ds/rtk_hciattach 等)**先不加**,
> 保持默认镜像最小,后续各外设章按需添加再编译,方便教学。

### 编译

```shell
cd <sdk>
. tools/scripts/setup-env.sh
source build/envsetup.sh
lunch          # 选择 t113-zgl_board(新加的选项)

./build.sh bootloader           # U-Boot/boot0(首次编译必须;pack 不自己编 bootloader,
                                # 缺它 pack 会报 u-boot.fex cant be open)
./build.sh kernel -j8            # 内核 + 模块 + boot.img
./build.sh openwrt_rootfs -j1    # rootfs(必须 -j1:并行会踩全志 host 工具的 jobserver 坑)
./build.sh pack                  # 打包
```

> 说明:`lunch` 也可以直接带参数 `lunch t113-zgl_board`;全量首次编译较久(>1h),后续单改内核用 `./build.sh kernel -j8` 增量即可。**这一步先编译出一份默认镜像**(串口可用),后续外设章逐个添加功能再重编。

<!-- 截图: 编译成功 -->

### 烧录镜像

产物:`out/t113_linux_zgl_board_uart0.img`

- **TF 卡烧写:用全志 PhoenixCard**。注意两点:
  1. 一定要选择烧写的卡是**启动卡**,而不是默认的**量产卡**(选错会导致 boot0 读不到启动介质,报 `MMC init failed → spinand read id failed: -110 → RESET` 死循环);
  2. PhoenixSuit 是 **flash(nand/emmc)烧写**用的工具,不要用它烧 TF 卡。
- **flash(nand)烧写:用 AllwinnertechPhoeniSuit**,先按住 load,再按 rst 后松开,最后松开 load。

烧卡后将 TF 卡插到板卡上电,串口(见下一章)应看到系统启动。

<!-- 截图: PhoenixCard 选启动卡 -->

## 外设适配

### 串口(UART3 调试口)

原理图 U24 调试座(3P)接的是 UART3(PB6/PB7),U-Boot 的调试口由 `sys_config.fex` 的 `[uart_para]` 决定。

#### 修改 U-Boot 调试串口

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/sys_config.fex
```

修改内容 —— diff(原值 → 新值,看差异):

```diff
 [uart_para]
 uart_debug_port = 3
-uart_debug_tx   = port:PG08<5><1><default><default>
+uart_debug_tx   = port:PB06<7><1><default><default>
-uart_debug_rx   = port:PG09<5><1><default><default>
+uart_debug_rx   = port:PB07<7><1><default><default>
```

修改后 `[uart_para]` 段的最终内容(直接复制):

```shell
[uart_para]
uart_debug_port = 3
uart_debug_tx   = port:PB06<7><1><default><default>
uart_debug_rx   = port:PB07<7><1><default><default>
```

> 说明:PB6/PB7 复用功能 7 = UART3(原值 PG08/PG09 mux5 是 evb1 模板的调试口)。

U-Boot 的 `CONFIG_CONS_INDEX=4`(`brandy/brandy-2.0/u-boot-2018/configs/sun8iw20p1_auto_defconfig`,1-based 第 4 个 NS16550 = UART3)不用改,pin 从 sys_config.fex 读取。

#### 修改内核调试串口

内核侧调试口走设备树。`kernel/linux-5.4/arch/arm/boot/dts/sun8iw20p1.dtsi` 里 `uart3: uart@2500c00` 节点(SDK 自带,不用改);板级的 pin 复用和使能在 `board.dts`:

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts
```

修改内容 1 —— pinctrl 组从 PG8/PG9 改成 PB6/PB7,diff:

```diff
 	uart3_pins_a: uart3_pins@0 {  /* For t113_evb */
-		pins = "PG8", "PG9";
+		pins = "PB6", "PB7";
 		function = "uart3";
 		drive-strength = <10>;
 		bias-pull-up;
 	};
 
 	uart3_pins_b: uart3_pins@1 {  /* For t113_evb */
-		pins = "PG8", "PG9";
+		pins = "PB6", "PB7";
 		function = "gpio_in";
 	};
```

pinctrl 组的最终内容(直接复制):

```shell
	uart3_pins_a: uart3_pins@0 {  /* For t113_evb */
		pins = "PB6", "PB7";          /* 调试串口 UART3 (U24 座) */
		function = "uart3";
		drive-strength = <10>;
		bias-pull-up;
	};

	uart3_pins_b: uart3_pins@1 {  /* For t113_evb */
		pins = "PB6", "PB7";
		function = "gpio_in";
	};
```

修改内容 2 —— `&uart3` 使能 + `&twi3` 禁用(PB6/PB7 让给 UART3),diff:

```diff
 &uart3 {
 	pinctrl-names = "default", "sleep";
 	pinctrl-0 = <&uart3_pins_a>;
 	pinctrl-1 = <&uart3_pins_b>;
 	status = "okay";
 };
 
 &twi3 {
 	clock-frequency = <400000>;
 	pinctrl-0 = <&twi3_pins_a>;
 	pinctrl-1 = <&twi3_pins_b>;
 	pinctrl-names = "default", "sleep";
-	status = "okay";
+	status = "disabled";
 };
```

`&twi3` 节点的最终内容(直接复制;`&uart3` 本来就是 okay 不用动):

```shell
&twi3 {
	clock-frequency = <400000>;
	pinctrl-0 = <&twi3_pins_a>;
	pinctrl-1 = <&twi3_pins_b>;
	pinctrl-names = "default", "sleep";
	status = "disabled";              /* AC108 不在板上,PB6/PB7 让给 UART3 */
};
```

#### 修改 U-Boot 环境变量(earlycon)

三个 `env.cfg` 的 earlycon 地址要指向 UART3 基址 `0x02500c00`(evb1 模板默认 `0x05000000` = UART0,earlycon 打印会打到错误的串口):

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/openwrt/env.cfg
```

修改内容 —— diff:

```diff
-earlycon=uart8250,mmio32,0x05000000
+earlycon=uart8250,mmio32,0x02500c00
 console=ttyS3,115200
```

修改后的最终内容(直接复制):

```shell
earlycon=uart8250,mmio32,0x02500c00     /* UART3 基址 */
console=ttyS3,115200
```

> 同样修改 `zgl_board/bsp/env.cfg` 和 `zgl_board/buildroot/env.cfg`(一致性;实际打包用的是 openwrt 的这个,pack 日志里 `Use u-boot env file` 可以确认)。

内核配置 `config-5.4` 里 `CONFIG_DEBUG_SUNXI_UART3=y`、`CONFIG_DEBUG_UART_PHYS=0x02500c00` 模板已正确,不用改。

#### 重新编译打包

```shell
./build.sh kernel -j8
./build.sh pack
```

#### 测试

串口线接法(USB-TTL 3.3V,关掉流控):

```
板子 U24 调试座(3P)        USB-TTL(3.3V)
Pin 1  UART3_RX (PB7)  ←→  TX
Pin 2  UART3_TX (PB6)  ←→  RX
Pin 3  GND             ←→  GND
```

```shell
minicom -D /dev/ttyUSB0 -b 115200
```

冷启动预期日志顺序:

```
1. HELLO! BOOT0 is starting!
2. HELLO! SBOOT is starting!
3. U-Boot 2018.07-...
4. Linux version 5.4.61-...      ← earlycon 修好后可见
5. Please press Enter to activate this console
```

系统起来后验证:

```shell
cat /proc/cmdline          # 应含 console=ttyS3,115200 earlycon=uart8250,mmio32,0x02500c00
dmesg | grep -E 'ttyS|sunxi-uart'   # 应见 uart3 注册(0x02500c00)
```

<!-- 截图: 串口启动日志 -->

### MIPI 屏(D240N2501V1 / ST7701SN,480x640)

SDK 内核树没有这块屏的面板驱动,需要完整移植。本屏关键约束(决定 dts 参数):

| 约束 | 值 | 原因 |
|---|---|---|
| `lcd_dclk_freq` | **≥ 28MHz** | T113 DSI PHY 每 lane ≥325Mbps:2-lane RGB888 下 `dclk×24/2≥325M → dclk≥28`;21MHz 时屏只亮背光,dmesg 报 `pclk is too small` |
| `lcd_dsi_if` | **0(视频模式)** | burst 模式带宽不够 |
| `lcd_hspw` | **≥4** | DSI `dsi_hsa=hspw*3-10`,<4 时为负 |
| `lcd_vt/vbp` | 850/62 | 刷新率≈65Hz,给 DSI FIFO 预填时间,治顶部花屏 |

#### 内核驱动移植

##### 添加驱动

```shell
cd <sdk>/kernel/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/lcd
touch st7701sn_d240n2501v1.c
```

**st7701sn_d240n2501v1.c**(本板屏幕的驱动,44 条初始化指令;完成后会写标记文件 `/tmp/st7701sn_init` 方便验证):

```c
/* drivers/video/sunxi/disp2/disp/lcd/st7701sn_d240n2501v1.c
 *
 * Panel driver for DX D240N2501V1 (2.4" 480x640 IPS TFT, ST7701SN,
 * 2-lane MIPI DSI Video Mode).
 *
 * Init sequence adapted from vendor RK3566 reference (D240C2501V1 family,
 * same ST7701SN command set).
 *
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "panels.h"
#include <linux/fs.h>

static void lcd_power_on(u32 sel);
static void lcd_power_off(u32 sel);
static void lcd_bl_open(u32 sel);
static void lcd_bl_close(u32 sel);

static void lcd_panel_init(u32 sel);
static void lcd_panel_exit(u32 sel);

#define panel_reset(sel, val) sunxi_lcd_gpio_set_value(sel, 0, val)

static void lcd_cfg_panel_info(struct panel_extend_para *info)
{
	u32 i = 0, j = 0;
	u32 items;
	u8 lcd_gamma_tbl[][2] = {
	    {0, 0},     {15, 15},   {30, 30},   {45, 45},   {60, 60},
	    {75, 75},   {90, 90},   {105, 105}, {120, 120}, {135, 135},
	    {150, 150}, {165, 165}, {180, 180}, {195, 195}, {210, 210},
	    {225, 225}, {240, 240}, {255, 255},
	};

	u32 lcd_cmap_tbl[2][3][4] = {
	    {
		{LCD_CMAP_G0, LCD_CMAP_B1, LCD_CMAP_G2, LCD_CMAP_B3},
		{LCD_CMAP_B0, LCD_CMAP_R1, LCD_CMAP_B2, LCD_CMAP_R3},
		{LCD_CMAP_R0, LCD_CMAP_G1, LCD_CMAP_R2, LCD_CMAP_G3},
	    },
	    {
		{LCD_CMAP_B3, LCD_CMAP_G2, LCD_CMAP_B1, LCD_CMAP_G0},
		{LCD_CMAP_R3, LCD_CMAP_B2, LCD_CMAP_R1, LCD_CMAP_B0},
		{LCD_CMAP_G3, LCD_CMAP_R2, LCD_CMAP_G1, LCD_CMAP_R0},
	    },
	};

	items = sizeof(lcd_gamma_tbl) / 2;
	for (i = 0; i < items - 1; i++) {
		u32 num = lcd_gamma_tbl[i + 1][0] - lcd_gamma_tbl[i][0];

		for (j = 0; j < num; j++) {
			u32 value = 0;

			value =
			    lcd_gamma_tbl[i][1] +
			    ((lcd_gamma_tbl[i + 1][1] - lcd_gamma_tbl[i][1]) *
			     j) /
			    num;
			info->lcd_gamma_tbl[lcd_gamma_tbl[i][0] + j] =
			    (value << 16) + (value << 8) + value;
		}
	}
	info->lcd_gamma_tbl[255] = (lcd_gamma_tbl[items - 1][1] << 16) +
				   (lcd_gamma_tbl[items - 1][1] << 8) +
				   lcd_gamma_tbl[items - 1][1];

	memcpy(info->lcd_cmap_tbl, lcd_cmap_tbl, sizeof(lcd_cmap_tbl));
}

static s32 lcd_open_flow(u32 sel)
{
	LCD_OPEN_FUNC(sel, lcd_power_on, 100);
	LCD_OPEN_FUNC(sel, lcd_panel_init, 120);
	LCD_OPEN_FUNC(sel, sunxi_lcd_tcon_enable, 120);
	LCD_OPEN_FUNC(sel, lcd_bl_open, 0);
	return 0;
}

static s32 lcd_close_flow(u32 sel)
{
	LCD_CLOSE_FUNC(sel, lcd_bl_close, 0);
	LCD_CLOSE_FUNC(sel, lcd_panel_exit, 200);
	LCD_CLOSE_FUNC(sel, sunxi_lcd_tcon_disable, 0);
	LCD_CLOSE_FUNC(sel, lcd_power_off, 500);

	return 0;
}

static void lcd_power_on(u32 sel)
{
	printk("st7701sn: lcd_power_on\n");
	sunxi_lcd_pin_cfg(sel, 1);
	sunxi_lcd_power_enable(sel, 0);
	sunxi_lcd_delay_ms(10);

	/* reset lcd by gpio (ACTIVE_LOW: drive 0 = reset asserted, 1 = running) */
	panel_reset(sel, 1);
	sunxi_lcd_delay_ms(5);
	panel_reset(sel, 0);
	sunxi_lcd_delay_ms(10);
	panel_reset(sel, 1);
	sunxi_lcd_delay_ms(120);
}

static void lcd_power_off(u32 sel)
{
	sunxi_lcd_pin_cfg(sel, 0);
	sunxi_lcd_delay_ms(20);
	panel_reset(sel, 0);
	sunxi_lcd_delay_ms(5);
	sunxi_lcd_power_disable(sel, 0);
}

static void lcd_bl_open(u32 sel)
{
	sunxi_lcd_pwm_enable(sel);
	sunxi_lcd_backlight_enable(sel);
}

static void lcd_bl_close(u32 sel)
{
	sunxi_lcd_backlight_disable(sel);
	sunxi_lcd_pwm_disable(sel);
}

/*
 * Sentinel opcodes for the init table.
 *   0xFF is used by ST7701SN for bank switching (selecting the page of
 *   registers we want to write to next), so it CANNOT be used as a sentinel.
 *   0xFE is reserved by the DCS spec for manufacturer commands and is
 *   unused by ST7701SN — safe to use as a delay marker.
 *   0xFC is "Read memory continue" (only valid in read mode) — safe
 *   end-of-table marker.
 */
#define REGFLAG_DELAY         0xFE
#define REGFLAG_END_OF_TABLE  0xFC

struct LCM_setting_table {
	u8 cmd;
	u32 count;
	u8 para_list[64];
};

/*
 * ST7701SN init sequence for D240N2501V1 family.
 * Sourced from RK3566 vendor reference (D240C2501V1 timing file).
 * The panel requires multiple bank switches (FF 77 01 00 00 XX)
 * to access extended command sets.
 */
static struct LCM_setting_table lcm_initialization_setting[] = {
	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13} },
	{0xEF, 1, {0x08} },

	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10} },
	{0xC0, 2, {0x4F, 0x00} },
	{0xC1, 2, {0x10, 0x0C} },
	{0xC2, 2, {0x37, 0x04} },
	{0xC7, 1, {0x00} }, /* x-dir rotate 0 */
	{0xCC, 1, {0x10} },

	{0xB0, 16, {0x00, 0x0B, 0x13, 0x0D, 0x10, 0x07, 0x02, 0x08,
		    0x07, 0x1F, 0x04, 0x11, 0x0F, 0x28, 0x2F, 0x1F} },
	{0xB1, 16, {0x00, 0x0C, 0x13, 0x0C, 0x10, 0x05, 0x02, 0x08,
		    0x08, 0x1E, 0x05, 0x13, 0x11, 0x27, 0x30, 0x1F} },

	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11} }, /* switch to BK1 */

	{0xB0, 1, {0x4A} }, /* VOP */
	{0xB1, 1, {0x4A} }, /* VCOM */
	{0xB2, 1, {0x87} }, /* VGH */
	{0xB3, 1, {0x80} },
	{0xB5, 1, {0x45} }, /* VGL */
	{0xB7, 1, {0x85} },
	{0xB8, 1, {0x20} },
	{0xC1, 1, {0x78} },
	{0xC2, 1, {0x78} },
	{0xD0, 1, {0x88} },
	{REGFLAG_DELAY, 100, {} },

	{0xE0, 3, {0x00, 0x00, 0x02} },
	{0xE1, 11, {0x04, 0xB0, 0x06, 0xB0, 0x05, 0xB0,
		    0x05, 0xB0, 0x00, 0x44, 0x44} },
	{0xE2, 12, {0x20, 0x20, 0x44, 0x44, 0x96, 0xA0,
		    0x00, 0x00, 0x96, 0xA0, 0x00, 0x00} },
	{0xE3, 4, {0x00, 0x00, 0x22, 0x22} },
	{0xE4, 2, {0x44, 0x44} },
	{0xE5, 16, {0x0C, 0x90, 0xB0, 0xA0, 0x0E, 0x92, 0xB0, 0xA0,
		    0x08, 0x8C, 0xB0, 0xA0, 0x0A, 0x8E, 0xB0, 0xA0} },
	{0xE6, 4, {0x00, 0x00, 0x22, 0x22} },
	{0xE7, 2, {0x44, 0x44} },
	{0xE8, 16, {0x0D, 0x91, 0xB0, 0xA0, 0x0F, 0x93, 0xB0, 0xA0,
		    0x09, 0x8D, 0xB0, 0xA0, 0x0B, 0x8F, 0xB0, 0xA0} },
	{0xE9, 2, {0x36, 0x44} },
	{0xEB, 7, {0x00, 0x00, 0xE4, 0xE4, 0x44, 0x00, 0x40} },
	{0xED, 16, {0xC1, 0xA2, 0xBF, 0x0F, 0x67, 0x45,
		    0xFF, 0xFF, 0xFF, 0xFF, 0x54, 0x76, 0xF0, 0xFB, 0x2A, 0x1C} },
	{0xEF, 6, {0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F} },

	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13} },
	{0xE8, 2, {0x00, 0x0E} },

	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00} },
	{0x11, 0, {} },
	{REGFLAG_DELAY, 120, {} },

	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13} },
	{0xE8, 2, {0x00, 0x0C} },
	{REGFLAG_DELAY, 20, {} },
	{0xE8, 2, {0x00, 0x00} },

	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00} },

	{0x36, 1, {0x00} }, /* U&D Y-DIR rotate 0 */
	{REGFLAG_DELAY, 20, {} },

	{0x29, 0, {} },     /* display on */
	{REGFLAG_DELAY, 20, {} },

	{REGFLAG_END_OF_TABLE, 0, {} }
};

static void lcd_panel_init(u32 sel)
{
	u32 i = 0;
	u32 n_cmds = 0, n_delay = 0;
	struct file *fp;

	sunxi_lcd_dsi_clk_enable(sel);
	sunxi_lcd_delay_ms(100);
	printk("st7701sn: lcd_panel_init begin\n");

	/* 调试:开始时清掉标记文件(写 "0") */
	fp = filp_open("/tmp/st7701sn_init", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (!IS_ERR(fp)) {
		kernel_write(fp, "0", 1, &fp->f_pos);
		filp_close(fp, NULL);
	}

	for (i = 0;; i++) {
		if (lcm_initialization_setting[i].cmd == REGFLAG_END_OF_TABLE) {
			printk("st7701sn: init done, %u cmds %u delays\n", n_cmds, n_delay);
			break;
		}
		else if (lcm_initialization_setting[i].cmd == REGFLAG_DELAY) {
			sunxi_lcd_delay_ms(lcm_initialization_setting[i].count);
			n_delay++;
		}
		else {
			dsi_dcs_wr(0, lcm_initialization_setting[i].cmd,
				   lcm_initialization_setting[i].para_list,
				   lcm_initialization_setting[i].count);
			n_cmds++;
		}
	}

	/* 调试:完成后写标记文件(让 adb 能验证 init sequence 真跑了) */
	fp = filp_open("/tmp/st7701sn_init", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (!IS_ERR(fp)) {
		char buf[64];
		int len = snprintf(buf, sizeof(buf), "OK %u cmds %u delays\n", n_cmds, n_delay);
		kernel_write(fp, buf, len, &fp->f_pos);
		filp_close(fp, NULL);
	}
}

static void lcd_panel_exit(u32 sel)
{
	sunxi_lcd_dsi_dcs_write_0para(sel, 0x28);
	sunxi_lcd_delay_ms(120);
	sunxi_lcd_dsi_dcs_write_0para(sel, 0x10);
	sunxi_lcd_delay_ms(120);
}

/*sel: 0:lcd0; 1:lcd1*/
static s32 lcd_user_defined_func(u32 sel, u32 para1, u32 para2, u32 para3)
{
	return 0;
}

struct __lcd_panel st7701sn_d240n2501v1_panel = {
	/* panel driver name, must match lcd_driver_name in board.dts */
	.name = "st7701sn_d240n2501v1",
	.func = {
		.cfg_panel_info = lcd_cfg_panel_info,
		.cfg_open_flow = lcd_open_flow,
		.cfg_close_flow = lcd_close_flow,
		.lcd_user_defined_func = lcd_user_defined_func,
	},
};
```


##### 修改 panels.c(注册面板)

```shell
vi <sdk>/kernel/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/lcd/panels.c
```

修改内容(在 `panel_array[]` 的 `&b080uan01_panel,` 之后追加),diff:

```diff
 #ifdef CONFIG_LCD_SUPPORT_B080UAN01_MIPI1200X1920
 	&b080uan01_panel,
 #endif
+#ifdef CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1
+	&st7701sn_d240n2501v1_panel,
+#endif
 	&super_lcd_panel,
 	NULL,
```

追加的内容(直接复制,插入在 `&b080uan01_panel,` 的 `#endif` 之后):

```c
#ifdef CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1
	&st7701sn_d240n2501v1_panel,
#endif
```

##### 修改 panels.h(extern 声明)

```shell
vi <sdk>/kernel/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/lcd/panels.h
```

修改内容(在 `extern struct __lcd_panel b080uan01_panel;` 的 `#endif` 之后追加),diff:

```diff
 #ifdef CONFIG_LCD_SUPPORT_B080UAN01_MIPI1200X1920
 extern struct __lcd_panel b080uan01_panel;
 #endif
+#ifdef CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1
+extern struct __lcd_panel st7701sn_d240n2501v1_panel;
+#endif
```

追加的内容(直接复制):

```c
#ifdef CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1
extern struct __lcd_panel st7701sn_d240n2501v1_panel;
#endif
```

##### 修改 Makefile

```shell
vi <sdk>/kernel/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/Makefile
```

修改内容(在 `disp-$(CONFIG_LCD_SUPPORT_B080UAN01_MIPI1200X1920)` 行之后添加一行),diff:

```diff
 disp-$(CONFIG_LCD_SUPPORT_B080UAN01_MIPI1200X1920) += lcd/b080uan01_mipi1200x1920.o
+disp-$(CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1) += lcd/st7701sn_d240n2501v1.o
 disp-y += lcd/super_lcd_driver.o
```

追加的一行(直接复制):

```makefile
disp-$(CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1) += lcd/st7701sn_d240n2501v1.o
```

##### 修改 Kconfig

```shell
vi <sdk>/kernel/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/lcd/Kconfig
```

修改内容(文件末尾追加),diff:

```diff
+config LCD_SUPPORT_ST7701SN_D240N2501V1
+	bool "LCD support st7701sn_d240n2501v1 panel (DX 2.4\" 480x640 IPS, ST7701SN, 2-lane MIPI)"
+	default n
+	help
+		If you want to support D240N2501V1 panel for display driver, select it.
```

追加的内容(直接复制,粘贴到文件末尾):

```shell
config LCD_SUPPORT_ST7701SN_D240N2501V1
	bool "LCD support st7701sn_d240n2501v1 panel (DX 2.4\" 480x640 IPS, ST7701SN, 2-lane MIPI)"
	default n
	help
		If you want to support D240N2501V1 panel for display driver, select it.
```

##### 显示修复 1:disp_display.c(治开机 DSI 半初始化/帧率低)

开机时 `bsp_disp_open()` 是空桩(U-Boot `lcd_used=0` 后面板不在 boot 阶段打开),DSI 处于半配置状态(fps < ref、TCON 错误计数攀升)。补丁:系统起来 3 秒后自动做一次 disable+enable。

```shell
vi <sdk>/kernel/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/de/disp_display.c
```

修改内容 1 —— 文件头部 `#include` 区加 workqueue 头文件,diff:

```diff
 #include "disp_display.h"
 #include "disp_rtwb.h"
 #include "../disp_trace.h"
+#include <linux/workqueue.h>
```

追加的一行(直接复制):

```c
#include <linux/workqueue.h>
```

修改内容 2 —— `bsp_disp_open()` 函数之后、`bsp_disp_close()` 之前插入(约 125 行处),diff:

```diff
+/*
+ * The boot-time LCD enable (start_work at probe) leaves the DSI in a
+ * half-configured state (fps < ref, TCON error counter climbing), and a
+ * later manual "disable; enable" via dispdbg always fixes it.  So after
+ * the system has settled, do the same disable+enable cycle ourselves.
+ */
+static void sunxi_disp_reinit_work(struct work_struct *work)
+{
+	u32 num_screens = bsp_disp_feat_get_num_screens();
+	u32 disp;
+
+	pr_info("[DISP] sunxi_disp_reinit_work: re-enabling LCDs\n");
+	for (disp = 0; disp < num_screens; disp++) {
+		struct disp_device *lcd = disp_get_lcd(disp);
+		if (!lcd)
+			continue;
+		if (lcd->is_enabled && lcd->is_enabled(lcd) && lcd->disable)
+			lcd->disable(lcd);
+		if (lcd->enable)
+			lcd->enable(lcd);
+	}
+}
+static DECLARE_DELAYED_WORK(sunxi_disp_reinit_work_item,
+			    sunxi_disp_reinit_work);
+
+static int __init sunxi_disp_late_init(void)
+{
+	pr_info("[DISP] sunxi_disp_late_init: schedule LCD re-init in 3s\n");
+	schedule_delayed_work(&sunxi_disp_reinit_work_item,
+			      msecs_to_jiffies(3000));
+	return 0;
+}
+late_initcall(sunxi_disp_late_init);
```

插入的代码块(直接复制,插入在 `bsp_disp_open()` 的 `}` 之后):

```c
/*
 * The boot-time LCD enable (start_work at probe) leaves the DSI in a
 * half-configured state (fps < ref, TCON error counter climbing), and a
 * later manual "disable; enable" via dispdbg always fixes it.  So after
 * the system has settled, do the same disable+enable cycle ourselves.
 */
static void sunxi_disp_reinit_work(struct work_struct *work)
{
	u32 num_screens = bsp_disp_feat_get_num_screens();
	u32 disp;

	pr_info("[DISP] sunxi_disp_reinit_work: re-enabling LCDs\n");
	for (disp = 0; disp < num_screens; disp++) {
		struct disp_device *lcd = disp_get_lcd(disp);
		if (!lcd)
			continue;
		if (lcd->is_enabled && lcd->is_enabled(lcd) && lcd->disable)
			lcd->disable(lcd);
		if (lcd->enable)
			lcd->enable(lcd);
	}
}
static DECLARE_DELAYED_WORK(sunxi_disp_reinit_work_item,
			    sunxi_disp_reinit_work);

static int __init sunxi_disp_late_init(void)
{
	pr_info("[DISP] sunxi_disp_late_init: schedule LCD re-init in 3s\n");
	schedule_delayed_work(&sunxi_disp_reinit_work_item,
			      msecs_to_jiffies(3000));
	return 0;
}
late_initcall(sunxi_disp_late_init);
```

##### 显示修复 2:dev_fb.c(治半屏)

U-Boot 即使 `lcd_used=0` 也会注入 `boot_disp` 几何信息给内核,fb 层会把 `screen_win` 错误设置成 280x1424(半屏)。补丁强制 fb 层为全屏几何。

```shell
vi <sdk>/kernel/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/dev_fb.c
```

修改内容 1 —— `display_fb_request` 函数里(`CONFIG_SUNXI_DISP2_FB_ROTATION_SUPPORT` 的 else 块结束后,约 2155 行),diff:

```diff
 			}
 
+			/* zgl: 强制层为全屏 fb 几何, 修复 screen_win 被错误设置(280x1424) */
+			config.info.screen_win.width = src_width;
+			config.info.screen_win.height = src_height;
 
 			config.info.mode = LAYER_MODE_BUFFER;
```

插入的代码块(直接复制,插入在 `}` 空行之后、`config.info.mode = LAYER_MODE_BUFFER;` 之前):

```c
			/* zgl: 强制层为全屏 fb 几何, 修复 screen_win 被错误设置(280x1424) */
			config.info.screen_win.width = src_width;
			config.info.screen_win.height = src_height;
```

修改内容 2 —— `sunxi_fb_pan_display` 里 crop 计算之后(约 722 行),diff:

```diff
 				config.info.fb.crop.height =
 				    ((long long)(var->yres / buffer_num)) << 32;
 
+				/* zgl: 强制层为全屏几何, 修复 screen_win=280x1424 / addr=0 */
+				config.info.screen_win.x = 0;
+				config.info.screen_win.y = 0;
+				config.info.screen_win.width =
+				    config.info.fb.crop.width >> 32;
+				config.info.screen_win.height =
+				    config.info.fb.crop.height >> 32;
+				if (info->fix.smem_start)
+					config.info.fb.addr[0] =
+					    (unsigned long long)info->fix.smem_start;
+				printk("[FB-DBG] pan_display %d: screen_win=%ux%u addr0=%llx\n",
+				       info->node, config.info.screen_win.width,
+				       config.info.screen_win.height,
+				       (unsigned long long)config.info.fb.addr[0]);
```

插入的代码块(直接复制,插入在 crop.height 计算之后):

```c
				/* zgl: 强制层为全屏几何, 修复 screen_win=280x1424 / addr=0 */
				config.info.screen_win.x = 0;
				config.info.screen_win.y = 0;
				config.info.screen_win.width =
				    config.info.fb.crop.width >> 32;
				config.info.screen_win.height =
				    config.info.fb.crop.height >> 32;
				if (info->fix.smem_start)
					config.info.fb.addr[0] =
					    (unsigned long long)info->fix.smem_start;
				printk("[FB-DBG] pan_display %d: screen_win=%ux%u addr0=%llx\n",
				       info->node, config.info.screen_win.width,
				       config.info.screen_win.height,
				       (unsigned long long)config.info.fb.addr[0]);
```

##### 内核 Menuconfig 配置

> **前置条件(实测 2026-10-22)**:这里加的 `CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1=y`
> 是**新 Kconfig 符号**,必须先完成上面"内核驱动移植"里的全部 5 个接线文件
> (驱动 .c / panels.c / panels.h / disp Makefile / lcd Kconfig)再编译——
> Kconfig 没接线时 `./build.sh kernel` 的 merge_config 会**静默丢弃**新符号
> (不报错、镜像正常产出,但驱动不在内核里,屏幕不显示)。编译后自检:
>
> ```shell
> strings <sdk>/out/t113/zgl_board/openwrt/uImage | grep -c st7701sn
> # 预期: ≥1;0 = 接线没做,Kconfig 符号被丢
> ```

板级内核配置直接改 `config-5.4`:

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4
```

修改内容(文件末尾 `CONFIG_EARLY_PRINTK=y` 之后追加),diff:

```diff
 CONFIG_EARLY_PRINTK=y
+
+# zgl_board MIPI panel (D240N2501V1 / ST7701SN)
+CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1=y
+CONFIG_BACKLIGHT_CLASS_DEVICE=y
+CONFIG_BACKLIGHT_PWM=y
+CONFIG_BACKLIGHT_GPIO=y
```

追加的内容(直接复制,粘贴到文件末尾):

```shell
# zgl_board MIPI panel (D240N2501V1 / ST7701SN)
CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1=y
CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_BACKLIGHT_PWM=y
CONFIG_BACKLIGHT_GPIO=y
```

##### 内核设备树配置

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts
```

修改内容 1 —— `&lcd0` 节点整体替换(evb1 原来是 default_lcd/LVDS 52MHz):

```diff
 &lcd0 {
     lcd_used            = <1>;          // 启用 LCD
 
-    lcd_driver_name     = "default_lcd";
-    lcd_backlight       = <50>;
-    lcd_if              = <3>;                    /* LVDS 旧屏 */
-
-    lcd_x               = <1024>;
-    lcd_y               = <600>;
-    lcd_width           = <150>;
-    lcd_height          = <94>;
-    lcd_dclk_freq       = <52>;
-
-    lcd_pwm_used        = <1>;
-    lcd_pwm_ch          = <7>;
-    lcd_pwm_freq        = <50000>;
-    lcd_pwm_pol         = <1>;
-    lcd_pwm_max_limit   = <255>;
-
-    lcd_hbp             = <160>;
-    lcd_ht              = <1344>;
-    lcd_hspw            = <60>;
-    lcd_vbp             = <23>;
-    lcd_vt              = <638>;
-    lcd_vspw            = <10>;
-
-    lcd_lvds_if         = <0>;
-    lcd_lvds_colordepth = <0>;
-    lcd_lvds_mode       = <0>;
-    lcd_frm             = <1>;
-    lcd_hv_clk_phase    = <0>;
-    lcd_hv_sync_polarity= <0>;
-    lcd_gamma_en        = <0>;
-    lcd_bright_curve_en = <0>;
-    lcd_cmap_en         = <0>;
-
-    deu_mode            = <0>;
-    lcdgamma4iep        = <22>;
-    smart_color         = <90>;
-
-    pinctrl-0 = <&lvds0_pins_a>;
-    pinctrl-1 = <&lvds0_pins_b>;
+    lcd_driver_name     = "st7701sn_d240n2501v1";
+    lcd_backlight       = <50>;
+    lcd_if              = <4>;
+    lcd_dsi_if          = <0>;
+    lcd_dsi_lane        = <2>;
+    pinctrl-0 = <&dsi2lane_pins_a>;
+    pinctrl-1 = <&dsi2lane_pins_b>;
+    lcd_x               = <480>;
+    lcd_y               = <640>;
+    lcd_width           = <37>;
+    lcd_height          = <49>;
+    lcd_dclk_freq       = <28>;
+    lcd_hbp             = <18>;
+    lcd_ht              = <508>;
+    lcd_hspw            = <8>;
+    lcd_vbp             = <62>;
+    lcd_vt              = <850>;
+    lcd_vspw            = <2>;
+    lcd_pwm_used        = <1>;
+    lcd_pwm_ch          = <7>;
+    lcd_pwm_freq        = <10000>;
+    lcd_pwm_pol         = <0>;
+    lcd_pwm_max_limit   = <255>;
+    lcd_dsi_format      = <0>;
+    lcd_dsi_te          = <0>;
+    lcd_dsi_eotp        = <0>;
+    lcd_frm             = <0>;
+    lcd_gamma_en        = <0>;
+    lcd_cmap_en         = <0>;
+    lcd_gpio_0 = <&pio PD 20 GPIO_ACTIVE_LOW>;
 };
```

`&lcd0` 节点的最终内容(直接复制,整体替换原来的 `&lcd0 { ... };`):

```shell
&lcd0 {
    lcd_used            = <1>;          // 启用 LCD
    lcd_driver_name     = "st7701sn_d240n2501v1"; // 驱动名称 (DX 2.4" 480x640, ST7701SN)
    lcd_backlight       = <50>;
    lcd_if              = <4>;          // DSI 接口
    lcd_dsi_if          = <0>;          // video mode (non-burst): burst 模式 loop_n1 会为负, DSI 停摆

    lcd_dsi_lane        = <2>;          // 2-lane MIPI DSI
    pinctrl-0 = <&dsi2lane_pins_a>;
    pinctrl-1 = <&dsi2lane_pins_b>;

    lcd_x               = <480>;        // 水平像素
    lcd_y               = <640>;        // 垂直像素
    lcd_width           = <37>;         // 物理宽度 (mm)  datasheet: 36.72
    lcd_height          = <49>;         // 物理高度 (mm)  datasheet: 48.96
    lcd_dclk_freq       = <28>;         // DCLK 频率 (MHz): >=28 满足 T113 DSI PHY 下限 325Mbps/lane (28*24/2=336)

    lcd_hbp             = <18>;         // HBP + HSPW  (8 + 10)
    lcd_ht              = <508>;        // 水平总周期   (480 + 10 + 8 + 10)
    lcd_hspw            = <8>;          // 行同步脉宽 (>=4, 否则 dsi_hsa=hspw*3-10 为负)
    lcd_vbp             = <62>;         // VBP + VSPW  (60 + 2): 加大后留给 DSI FIFO 预填时间，治顶部花屏
    lcd_vt              = <850>;        // 垂直总周期: 702→850, 刷新率降到~65Hz, 给 DSI 带宽余量
    lcd_vspw            = <2>;          // 场同步脉宽

    lcd_pwm_used        = <1>;          // 启用背光 PWM
    lcd_pwm_ch          = <7>;          // PWM 通道 7 (PD22)
    lcd_pwm_freq        = <10000>;      // PWM 频率 (Hz)
    lcd_pwm_pol         = <0>;          // 极性：正极
    lcd_pwm_max_limit   = <255>;

    lcd_dsi_format      = <0>;          // RGB888
    lcd_dsi_te          = <0>;          // 禁用 TE 触发
    lcd_dsi_eotp        = <0>;          // 启用 EOT packet
    lcd_frm             = <0>;
    lcd_gamma_en        = <0>;
    lcd_cmap_en         = <0>;

    lcd_gpio_0 = <&pio PD 20 GPIO_ACTIVE_LOW>;  // LCD 复位引脚 (ST7701SN 低有效)
};
```

> `dsi2lane_pins_a/b` 在 `sun8iw20p1.dtsi` 里已定义(PD0~PD5,function=dsi,drive-strength=30),不用改。

修改内容 2 —— `&disp` 的 fb0 尺寸(匹配 480x640),diff:

```diff
-    fb0_width                = <1024>;
-    fb0_height               = <600>;
+    fb0_width                = <480>;
+    fb0_height               = <640>;
```

修改后的最终内容(直接复制):

```shell
    fb0_format               = <0>;   // ARGB8888
    fb0_buffer_num           = <2>;   // 双缓冲
    fb0_width                = <480>; // 帧缓冲宽度 (匹配 D240N2501V1 480x640)
    fb0_height               = <640>; // 帧缓冲高度
```

修改内容 3 —— 根节点下新增 backlight 节点(背光用 PWM7,亮度 0-255 线性表),diff(新增节点):

```diff
+	backlight: backlight {
+		compatible = "pwm-backlight";
+		pwms = <&pwm 7 10000 0>;
+		pwm-names = "backlight";
+		brightness-levels = <
+			0   1   2   3   4   5   6   7
+			(中间省略,完整 0-255 表见下方"最终内容")
+			248 249 250 251 252 253 254 255
+		>;
+		default-brightness-level = <100>;
+		status = "okay";
+	};
```

backlight 节点的最终内容(直接复制,粘贴到根节点下):

```shell
	backlight: backlight {
		compatible = "pwm-backlight";
		pwms = <&pwm 7 10000 0>;
		pwm-names = "backlight";
		brightness-levels = <
			0   1   2   3   4   5   6   7
			8   9  10  11  12  13  14  15
			16  17  18  19  20  21  22  23
			24  25  26  27  28  29  30  31
			32  33  34  35  36  37  38  39
			40  41  42  43  44  45  46  47
			48  49  50  51  52  53  54  55
			56  57  58  59  60  61  62  63
			64  65  66  67  68  69  70  71
			72  73  74  75  76  77  78  79
			80  81  82  83  84  85  86  87
			88  89  90  91  92  93  94  95
			96  97  98  99 100 101 102 103
			104 105 106 107 108 109 110 111
			112 113 114 115 116 117 118 119
			120 121 122 123 124 125 126 127
			128 129 130 131 132 133 134 135
			136 137 138 139 140 141 142 143
			144 145 146 147 148 149 150 151
			152 153 154 155 156 157 158 159
			160 161 162 163 164 165 166 167
			168 169 170 171 172 173 174 175
			176 177 178 179 180 181 182 183
			184 185 186 187 188 189 190 191
			192 193 194 195 196 197 198 199
			200 201 202 203 204 205 206 207
			208 209 210 211 212 213 214 215
			216 217 218 219 220 221 222 223
			224 225 226 227 228 229 230 231
			232 233 234 235 236 237 238 239
			240 241 242 243 244 245 246 247
			248 249 250 251 252 253 254 255
		>;
		default-brightness-level = <100>;
		status = "okay";
	};
```

#### U-Boot 设备树配置

U-Boot 即使 `lcd_used=0` 也会注入 `boot_disp` 几何信息给内核(半屏问题的根源),所以 U-Boot 侧参数要和内核**同步**:

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/uboot-board.dts
```

修改内容 —— `&lcd0` 节点(原 default_lcd/LVDS 52MHz 1024x600):

```diff
 &lcd0 {
-	lcd_used            = <1>;
-	lcd_driver_name     = "default_lcd";
-	lcd_backlight       = <50>;
-	lcd_if              = <3>;
-
-	lcd_x               = <1024>;
-	lcd_y               = <600>;
-	lcd_width           = <150>;
-	lcd_height          = <94>;
-	lcd_dclk_freq       = <52>;
-
-	lcd_pwm_used        = <1>;
-	lcd_pwm_ch          = <7>;
-	lcd_pwm_freq        = <50000>;
-	lcd_pwm_pol         = <1>;
-	lcd_pwm_max_limit   = <255>;
-
-	lcd_hbp             = <160>;
-	lcd_ht              = <1344>;
-	lcd_hspw            = <60>;
-	lcd_vbp             = <23>;
-	lcd_vt              = <638>;
-	lcd_vspw            = <10>;
-
-	lcd_lvds_if         = <0>;
-	lcd_lvds_colordepth = <0>;
-	lcd_lvds_mode       = <0>;
-	lcd_frm             = <1>;
-	lcd_hv_clk_phase    = <0>;
-	lcd_hv_sync_polarity= <0>;
-	lcd_gamma_en        = <0>;
-	lcd_bright_curve_en = <0>;
-	lcd_cmap_en         = <0>;
+	lcd_used            = <0>;
+	lcd_driver_name     = "st7701sn_d240n2501v1";
+	lcd_backlight       = <50>;
+	lcd_if              = <4>;
+	lcd_x               = <480>;
+	lcd_y               = <640>;
+	lcd_width           = <37>;
+	lcd_height          = <49>;
+	lcd_dclk_freq       = <28>;
+	lcd_pwm_used        = <1>;
+	lcd_pwm_ch          = <7>;
+	lcd_pwm_freq        = <10000>;
+	lcd_pwm_pol         = <0>;
+	lcd_pwm_max_limit   = <255>;
+	lcd_hbp             = <18>;
+	lcd_ht              = <508>;
+	lcd_hspw            = <8>;
+	lcd_vbp             = <62>;
+	lcd_vt              = <850>;
+	lcd_vspw            = <2>;
+	lcd_dsi_lane        = <2>;
+	lcd_dsi_if          = <0>;
+	lcd_dsi_format      = <0>;
+	lcd_dsi_te          = <0>;
+	lcd_frm             = <0>;
+	lcd_io_phase        = <0>;
+	lcd_gamma_en        = <0>;
+	lcd_bright_curve_en = <0>;
+	lcd_cmap_en         = <0>;
 };
```

`&lcd0` 节点的最终内容(直接复制,整体替换原来的 `&lcd0 { ... };`):

```shell
&lcd0 {
	lcd_used            = <0>;			// 禁用 U-Boot LCD 初始化；新屏驱动在 kernel 内
					// 参数已同步为 D240N2501V1 (480x640, ST7701SN), 避免 boot 显示信息泄漏旧屏几何

	lcd_driver_name     = "st7701sn_d240n2501v1";	// 占位 (lcd_used=0 不查找面板表)
	lcd_backlight       = <50>;
	lcd_if              = <4>;			// 0:rgb 4:dsi

	lcd_x               = <480>;
	lcd_y               = <640>;
	lcd_width           = <37>;
	lcd_height          = <49>;
	lcd_dclk_freq       = <28>;

	lcd_pwm_used        = <1>;
	lcd_pwm_ch          = <7>;
	lcd_pwm_freq        = <10000>;
	lcd_pwm_pol         = <0>;
	lcd_pwm_max_limit   = <255>;

	lcd_hbp             = <18>;
	lcd_ht              = <508>;
	lcd_hspw            = <8>;
	lcd_vbp             = <62>;
	lcd_vt              = <850>;
	lcd_vspw            = <2>;

	lcd_dsi_lane        = <2>;
	lcd_dsi_if          = <0>;
	lcd_dsi_format      = <0>;
	lcd_dsi_te          = <0>;
	lcd_frm             = <0>;
	lcd_io_phase        = <0>;
	lcd_gamma_en        = <0>;
	lcd_bright_curve_en = <0>;
	lcd_cmap_en         = <0>;
};
```

#### 重新编译打包

```shell
./build.sh kernel -j8
./build.sh pack
```

> 注意:面板驱动和显示修复是**内建**(=y)的,改一次要重新烧一次完整镜像,不能只推 .ko。

#### 测试

开发板上电后,检查 framebuffer 设备:

```shell
adb shell ls -l /dev/fb0
adb shell cat /sys/class/graphics/fb0/bits_per_pixel    # 预期: 32
```

面板初始化验证(驱动写的标记文件):

```shell
adb shell cat /tmp/st7701sn_init
# 预期: OK 44 cmds 5 delays
```

时序/帧率验证:

```shell
adb shell cat /sys/class/disp/disp/attr/sys
# 预期: ref_fps:65  fps:65.3  480x 640  frame[ 0, 0, 480, 640]
```

##### 花屏测试

```shell
# 全屏雪花:cat /dev/urandom 持续写入直到写满整个 fb(报 No space left on device 是正常现象,说明写满了):
adb shell 'cat /dev/urandom > /dev/fb0'
# 预期: 屏幕全部雪花;最后报 cat: write error: No space left on device(正常)
```

##### 彩条测试

```shell
# 先清屏(花屏测试写满的 fb 会残留,必须先 cat /dev/zero 覆盖掉再开彩条):
adb shell 'cat /dev/zero > /dev/fb0'
# 再开彩条:
adb shell 'echo 8 > /sys/class/disp/disp/attr/colorbar'
# 预期: 屏幕显示彩条
```

> 注意:`echo 0 > .../colorbar` **不会**关闭彩条并恢复 framebuffer 显示(实测彩条一直显示,
> 直到下一次 fb 写入/重启);要恢复画面直接重启。实测彩条显示正常即为通过判据。

<!-- 截图: 屏幕彩条/雪花 -->

##### 屏幕排错速查

| 现象 | 原因 | 处理 |
|---|---|---|
| 只亮背光无图像 | dclk < 28MHz | `lcd_dclk_freq` ≥ 28 |
| 帧率 < 65 / TCON 错误 | DSI 半初始化 | disp_display.c 补丁 |
| 半屏 | U-Boot 旧几何泄漏 | uboot-board.dts 同步 + dev_fb.c 补丁 |
| 颜色错 | 格式不对 | `lcd_dsi_format`(0=RGB888)/`lcd_frm` |

### 触摸(dx_touch,I2C 0x38)

原理图:触摸挂 I2C1(PE0=SCL/PE1=SDA),复位 PE4,中断 PE5。SDK 自带的 `dx_touch` 驱动是**旧版**,有一个必踩的坑(见下面驱动代码里的注释);本教程直接使用修复版。

#### 内核驱动移植

##### 添加驱动

```shell
cd <sdk>/kernel/linux-5.4/drivers/input/touchscreen
mkdir -p dx_touch
touch dx_touch/dx_touch.c
touch dx_touch/Makefile
```

**dx_touch/Makefile**(完整内容):

```makefile
obj-$(CONFIG_TOUCHSCREEN_DX_TOUCH) += dx_touch.o
```

**dx_touch/dx_touch.c**(完整驱动;与旧版的关键差异在复位序列末尾——`gpio_direction_input(ts->irq_pin)` 把中断脚切回输入,否则中断申请失败、触摸不工作;X 坐标镜像 `480-` 适配本屏):

```c
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/input/mt.h>
#include <linux/random.h>


#define lcdx    480
#define lcdy    640

#define FT_SWAP(x, y)                 do{\
                                         typeof(x) z = x;\
                                         x = y;\
                                         y = z;\
                                       }while (0)

#if 0
#define FT_DEBUG(fmt,arg...)  printk("DX_TOUCH:%s %d "fmt"",__FUNCTION__,__LINE__,##arg);
#else
#define FT_DEBUG(fmt,arg...)
#endif

struct dx_touch_dev {
    struct i2c_client *client;
    struct input_dev *input_dev;
    int rst_pin;
    int irq_pin;
    u32 abs_x_max;
    u32 abs_y_max;
    int irq;
};

s32 dx_touch_i2c_read(struct i2c_client *client,u8 *addr,u8 addr_len, u8 *buf, s32 len)
{
    struct i2c_msg msgs[2];
    s32 ret=-1;
    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = client->addr;
    msgs[0].len   = addr_len;
    msgs[0].buf   = &addr[0];
    msgs[1].flags = I2C_M_RD;
    msgs[1].addr  = client->addr;
    msgs[1].len   = len;
    msgs[1].buf   = &buf[0];

    ret = i2c_transfer(client->adapter, msgs, 2);
    if(ret == 2)return 0;

    if(addr_len == 2){
        FT_DEBUG("I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(addr[0] << 8)) | addr[1]), len, ret);
    }else {
        FT_DEBUG("I2C Read: 0x%02X, %d bytes failed, errcode: %d! Process reset.", addr[0], len, ret);
    }

    return -1;
}

s32 dx_touch_i2c_write(struct i2c_client *client, u8 *addr, u8 addr_len, u8 *buf,s32 len)
{
    struct i2c_msg msg;
    s32 ret = -1;
    u8 *temp_buf;

    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = len+addr_len;

    temp_buf= kzalloc(msg.len, GFP_KERNEL);
    if (!temp_buf){
        goto error;
    }

    // 装填地址
    memcpy(temp_buf, addr, addr_len);
    // 装填数据
    memcpy(temp_buf + addr_len, buf, len);
    msg.buf = temp_buf;

    ret = i2c_transfer(client->adapter, &msg, 1);
    if (ret == 1) {
        kfree(temp_buf);
        return 0;
    }

error:
    if(addr_len == 2){
        FT_DEBUG("I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(addr[0] << 8)) | addr[1]), len, ret);
    }else {
        FT_DEBUG("I2C Read: 0x%02X, %d bytes failed, errcode: %d! Process reset.", addr[0], len, ret);
    }
    if (temp_buf)
        kfree(temp_buf);
    return -1;
}

static irqreturn_t dx_touch_irq_handler(int irq, void *dev_id)
{
    s32 ret = -1;
    struct dx_touch_dev *ts = dev_id;
    u8 addr[1] = {0x02};
    u8 point_data[1+6*5]={0};//1个状态位置+5个触摸点,一个点是6个数据组成
    u8 touch_num = 0;
    u8 *touch_data;
    int i = 0;
    int event_flag, touch_id, input_x, input_y;

    FT_DEBUG("irq");

    ret = dx_touch_i2c_read(ts->client, addr,sizeof(addr), point_data, sizeof(point_data));
    if (ret < 0){
        FT_DEBUG("I2C write end_cmd error!");
    }
    touch_num = point_data[0]&0x0f;
    FT_DEBUG("touch_num:%d",touch_num);

    //获取

    for(i=0; i<5; i++){
        //获取点
        touch_data = &point_data[1+6*i];
        /*
        00b: Put Down
        01b: Put Up
        10b: Contact
        11b: Reserved
        */
        event_flag = touch_data[0] >> 6;
        if(event_flag == 0x03)continue;
        touch_id = touch_data[2] >> 4;

        FT_DEBUG("i:%d touch_id:%d event_flag:%d",i,touch_id,event_flag);
     //  input_x  = ((touch_data[0]&0x0f)<<8) | touch_data[1];
     input_x  = 480-(((touch_data[0]&0x0f)<<8) | touch_data[1]);
        input_y  = ((touch_data[2]&0x0f)<<8) | touch_data[3];

     //    FT_SWAP(input_x,input_y);//横屏时打开,竖屏时要屏蔽
        FT_DEBUG("i:%d,x:%d,y:%d",i,input_x,input_y);
        // 设定输入设备的触摸槽位
        input_mt_slot(ts->input_dev, touch_id);

        if(event_flag == 0){
            // 如果是按下上报按下和坐标
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_x);
            input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y);
        }else if (event_flag == 2){
            // 如果是长按直接上报数据
            input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_x);
            input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y);
        }else if(event_flag == 1){
            // 触摸抬起,上报事件
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
        }
    }

    // 报告输入设备的指针仿真信息
    input_mt_report_pointer_emulation(ts->input_dev, true);

    // 同步输入事件
    input_sync(ts->input_dev);

    return IRQ_HANDLED;
}


s32 ft5xx6_read_version(struct i2c_client *client)
{
    s32 ret = -1;
    u8 addr[1] = {0xA1};
    u8 buf[3] = {0};


    ret = dx_touch_i2c_read(client, addr,sizeof(addr), buf, sizeof(buf));
    if (ret < 0){
        FT_DEBUG("GTP read version failed");
        return ret;
    }

    if (buf[5] == 0x00){
        FT_DEBUG("IC Version: %0x %0x_%02x", buf[0], buf[1], buf[2]);
    }
    else{
        FT_DEBUG("IC Version: %0x %0x_%02x", buf[0], buf[1], buf[2]);
    }
    return ret;
}

static int dx_touch_ts_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
    int ret;
    struct dx_touch_dev *ts;
    struct device_node *np = client->dev.of_node;
    // 打印调试信息
    FT_DEBUG("locat");

    // ts = kzalloc(sizeof(*ts), GFP_KERNEL);
    ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
    if (ts == NULL){
        dev_err(&client->dev, "Alloc GFP_KERNEL memory failed.");
        return -ENOMEM;
    }
    ts->client = client;
    i2c_set_clientdata(client, ts);

    if (of_property_read_u32(np, "max-x", &ts->abs_x_max)) {
    	dev_err(&client->dev, "no max-x defined\n");
    	return -EINVAL;
    }
    FT_DEBUG("abs_x_max:%d",ts->abs_x_max);

    if (of_property_read_u32(np, "max-y", &ts->abs_y_max)) {
    	dev_err(&client->dev, "no max-y defined\n");
    	return -EINVAL;
    }
    FT_DEBUG("abs_x_max:%d",ts->abs_y_max);

    //找复位gpio
    ts->rst_pin = of_get_named_gpio(np, "reset-gpio", 0);
    //申请复位gpio
    ret = devm_gpio_request(&client->dev,ts->rst_pin,"dx touch touch gpio");
    if (ret < 0){
        dev_err(&client->dev, "gpio request failed.");
        return -ENOMEM;
    }

    //找中断引进
    ts->irq_pin = of_get_named_gpio(np, "touch-gpio", 0);
    /* 申请使用管脚 */
    ret = devm_gpio_request_one(&client->dev, ts->irq_pin,
                GPIOF_IN, "dx touch touch gpio");
    if (ret < 0)
        return ret;

    gpio_direction_output(ts->rst_pin,1);
    msleep(2);
    gpio_direction_output(ts->irq_pin,0);
    msleep(2);
    gpio_direction_output(ts->rst_pin,1);
    msleep(16);
    gpio_direction_input(ts->irq_pin);   /* 关键:把 IRQ pin 切回输入模式,否则 devm_request_threaded_irq 会失败 */
    msleep(50);

    //申请中断
    ts->irq = gpio_to_irq(ts->irq_pin);
    if(ts->irq){
        ret = devm_request_threaded_irq(&(client->dev), ts->irq, NULL,
            dx_touch_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT ,
            client->name, ts);
        if (ret != 0) {
            FT_DEBUG("Cannot allocate ts INT!ERRNO:%d\n", ret);
            return ret;
        }
    }

    // 分配输入设备对象
    ts->input_dev = devm_input_allocate_device(&client->dev);
    if (!ts->input_dev) {
        dev_err(&client->dev, "Failed to allocate input device.\n");
        return -ENOMEM;
    }

    // 设置输入设备的名称和总线类型
    ts->input_dev->name = "dx touch screen";
    ts->input_dev->id.bustype = BUS_I2C;

    /*设置触摸 x 和 y 的最大值*/
    // 设置输入设备的绝对位置参数
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, lcdx, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, lcdy, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

    // 初始化多点触摸设备的槽位
    ret = input_mt_init_slots(ts->input_dev, 5, INPUT_MT_DIRECT);
    if (ret) {
        dev_err(&client->dev, "Input mt init error\n");
        return ret;
    }

    // 注册输入设备
    ret = input_register_device(ts->input_dev);
    if (ret)
        return ret;

    ft5xx6_read_version(client);


    return 0;
}

static int dx_touch_ts_remove(struct i2c_client *client)
{
    struct dx_touch_dev *ts = i2c_get_clientdata(client);
    FT_DEBUG("locat");
    input_unregister_device(ts->input_dev);
    return 0;
}

static const struct of_device_id dx_touch_of_match[] = {
    { .compatible = "dx,touch", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dx_touch_of_match);


static struct i2c_driver dx_touch_ts_driver = {
    .probe      = dx_touch_ts_probe,
    .remove     = dx_touch_ts_remove,
    .driver = {
        .name     = "dx-touch",
	 .of_match_table = of_match_ptr(dx_touch_of_match),
    },
};

static int __init dx_ts_init(void)
{
    FT_DEBUG("locat");
    return i2c_add_driver(&dx_touch_ts_driver);
}

static void __exit dx_ts_exit(void)
{
    FT_DEBUG("locat");
	i2c_del_driver(&dx_touch_ts_driver);
}

module_init(dx_ts_init);
module_exit(dx_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dx touch driver");
MODULE_AUTHOR("pzjiao2008@126.com");
```

##### 修改 Kconfig

```shell
vi <sdk>/kernel/linux-5.4/drivers/input/touchscreen/Kconfig
```

修改内容(在 `config TOUCHSCREEN_IQS5XX` 块结束之后、`source "drivers/input/touchscreen/gslx680new/Kconfig"` 之前插入,约 1326 行),diff:

```diff
+config TOUCHSCREEN_DX_TOUCH
+	tristate "DX touch screen (D240N2501V1 panel)"
+	depends on I2C
+	help
+	  Say Y to enable support for the DX touch IC bundled with
+	  the D240N2501V1 2.4" 480x640 panel (ST7701SN).
+	  I2C address 0x38, INT active-low, RST active-high.
```

追加的内容(直接复制,插入在 IQS5XX 块之后):

```shell
config TOUCHSCREEN_DX_TOUCH
	tristate "DX touch screen (D240N2501V1 panel)"
	depends on I2C
	help
	  Say Y to enable support for the DX touch IC bundled with
	  the D240N2501V1 2.4" 480x640 panel (ST7701SN).
	  I2C address 0x38, INT active-low, RST active-high.
```

##### 修改 Makefile

```shell
vi <sdk>/kernel/linux-5.4/drivers/input/touchscreen/Makefile
```

修改内容(在 `obj-$(CONFIG_TOUCHSCREEN_IQS5XX)` 行之后追加),diff:

```diff
 obj-$(CONFIG_TOUCHSCREEN_IQS5XX)	+= iqs5xx.o
+obj-$(CONFIG_TOUCHSCREEN_DX_TOUCH) += dx_touch/
```

追加的一行(直接复制):

```makefile
obj-$(CONFIG_TOUCHSCREEN_DX_TOUCH) += dx_touch/
```

##### 内核 Menuconfig 配置

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4
```

修改内容(在 `CONFIG_TOUCHSCREEN_GT9XXNEW_TS=y` 之后追加),diff:

```diff
 CONFIG_TOUCHSCREEN_GT9XXNEW_TS=y
+CONFIG_TOUCHSCREEN_DX_TOUCH=y
```

追加的一行(直接复制):

```shell
CONFIG_TOUCHSCREEN_DX_TOUCH=y
```

> 说明:`CONFIG_TOUCHSCREEN_GT9XXNEW_TS=y` 可保留(不同 I2C 地址,未挂节点不会 probe,不冲突)。

#### 内核设备树配置

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts
```

修改内容 1 —— pinctrl 组 `twi1_pins_a/b` 引脚改成 PE0/PE1(evb1 模板默认 PE14/PE15),diff:

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

pinctrl 组的最终内容(直接复制):

```shell
	twi1_pins_a: twi1@0 {
		pins = "PE0", "PE1";          /* Touch I2C on zgl_board (SCL=PE0, SDA=PE1) */
		function = "twi1";
		drive-strength = <10>;
	};

	twi1_pins_b: twi1@1 {
		pins = "PE0", "PE1";
		function = "gpio_in";
	};
```

修改内容 2 —— `&twi1` 控制器使能 + 挂触摸节点,diff:

```diff
 &twi1 {
     clock-frequency = <400000>;
     pinctrl-0 = <&twi1_pins_a>;    // PE0(SCK), PE1(SDA)
     pinctrl-1 = <&twi1_pins_b>;
     pinctrl-names = "default", "sleep";
     status = "okay";
+
+    dxts@38 {
+        compatible = "dx,touch";
+        reg = <0x38>;
+        max-x = <480>;
+        max-y = <640>;
+        touch-gpio = <&pio PE 5 GPIO_ACTIVE_HIGH>;   /* INT */
+        reset-gpio = <&pio PE 4 GPIO_ACTIVE_HIGH>;   /* RST */
+        status = "okay";
+    };
 };
```

`&twi1` 节点的最终内容(直接复制,整体替换原来的 `&twi1 { ... };`):

```shell
&twi1 {
    clock-frequency = <400000>;
    pinctrl-0 = <&twi1_pins_a>;    // PE0(SCK), PE1(SDA)
    pinctrl-1 = <&twi1_pins_b>;
    pinctrl-names = "default", "sleep";
    status = "okay";

    dxts@38 {
        compatible = "dx,touch";
        reg = <0x38>;
        max-x = <480>;
        max-y = <640>;
        touch-gpio = <&pio PE 5 GPIO_ACTIVE_HIGH>;   /* INT */
        reset-gpio = <&pio PE 4 GPIO_ACTIVE_HIGH>;   /* RST */
        status = "okay";
    };
};
```

> **属性名陷阱**:`dx,touch` 驱动只认 `touch-gpio` / `reset-gpio`(见驱动代码里的
> `of_get_named_gpio(np, "touch-gpio", 0)`),**不要**写成主流触摸驱动的
> `interrupts = <...>` + `reset-gpios = <...>` 写法——两套属性混用会导致 probe 失败。

#### OpenWrt 配置(测试工具)

第一次给 defconfig 加外设包(环境章建的默认镜像没有这些工具),需要的包按外设章逐步添加:

```shell
vi <sdk>/openwrt/target/t113/t113-zgl_board/defconfig
```

修改内容 —— diff:

```diff
-# CONFIG_PACKAGE_evtest is not set
+CONFIG_PACKAGE_evtest=y
-# CONFIG_PACKAGE_i2c-tools is not set
+CONFIG_PACKAGE_i2c-tools=y
```

修改后的最终内容(直接复制,替换对应的两行):

```shell
CONFIG_PACKAGE_evtest=y
CONFIG_PACKAGE_i2c-tools=y
```

修改后的最终内容(直接复制):

```shell
CONFIG_PACKAGE_evtest=y
CONFIG_PACKAGE_i2c-tools=y
```

#### 重新编译打包

```shell
./build.sh kernel -j8
./build.sh openwrt_rootfs -j1
./build.sh pack
```

> 提示:如果编译时 `evtest` 源码包下载失败(死代理),手动下载放到 `openwrt/dl/`:
> `curl -fL -o openwrt/dl/evtest-1.34.tar.xz http://deb.debian.org/debian/pool/main/e/evtest/evtest_1.34.orig.tar.xz`
> (sha256 必须等于 evtest Makefile 里的 PKG_HASH:`e49f1f160b30c8f7c2a4caef5ab655f1caf816483d19fdedd6db2d251d7ab80e`)

#### 测试

```shell
# 1. I2C 设备
adb shell 'ls /dev/i2c-* && cat /sys/bus/i2c/devices/*/name'
# 预期: /dev/i2c-1;  1-0038 → "touch";  2502400.twi → "SUNXI TWI"

# 2. 0x38 被驱动占用(正常)
adb shell 'i2cdetect -y 1'
# 预期: 30: 行 0x38 位置显示 UU

# 3. 输入设备
adb shell 'cat /proc/bus/input/devices | grep -A8 "dx touch"'
# 预期: Name="dx touch screen"  Handlers=event4  B: KEY=400  B: ABS=2658000 3

# 4. 触摸事件
adb shell 'evtest /dev/input/event4'
# 触摸屏幕: 应见 EV_KEY/BTN_TOUCH、EV_ABS/ABS_X(0-480)/ABS_Y(0-640)、SYN_REPORT
```

<!-- 截图: evtest 触摸事件 -->

##### 方向校准速查(改 dx_touch.c 后重编内核)

| 现象 | 修改 |
|---|---|
| 左右反 | 去掉 `input_x = 480-(...)` 里的 `480-` 镜像 |
| 上下反 | `input_y = 640-(((touch_data[2]&0x0f)<<8)\|touch_data[3]);` |
| 横竖屏反 | 打开 `FT_SWAP(input_x,input_y)` |

### WiFi(RTL8723DS,SDIO)

原理图:WiFi 走 SDIO 总线 1(PG0~PG5);模块 WL_DIS#(Pin12)硬件上拉常使能,**不接 MCU**;32K 时钟模块内部自带。SDK 自带 8723ds 驱动源码(`kernel/linux-5.4/drivers/net/wireless/rtl8723ds/`,经 `drivers/net/wireless/Kconfig:53` 和 `Makefile:36` 挂进构建),只需要修 2 个内核文件 + 配置。

#### 修改内核 sunxi-rfkill.c(必改:probe 永久注销)

`module_platform_driver_probe()` 变体在设备因 supplier 未就绪而 defer 时会**直接注销驱动**,导致 8723ds 加载时报 `get wifi_sdc_id failed`。

```shell
vi <sdk>/kernel/linux-5.4/drivers/misc/sunxi-rf/sunxi-rfkill.c
```

修改内容(文件末尾),diff:

```diff
-module_platform_driver_probe(rfkill_driver, rfkill_probe);
+module_platform_driver(rfkill_driver);
```

修改后的最终内容(直接复制,替换文件末尾那一行):

```c
module_platform_driver(rfkill_driver);    /* 普通注册:允许 probe defer */
```

#### 修改内核 sunxi-wlan.c(必改:misc minor 耗尽)

开机时动态 minor 位图被用光(ion 排到 127 = 最后空位),`misc_register("sunxi-wlan")` 返回 -EBUSY,`wlan_data` 永远 NULL。

```shell
vi <sdk>/kernel/linux-5.4/drivers/misc/sunxi-rf/sunxi-wlan.c
```

修改内容(约 248 行),diff:

```diff
 static struct miscdevice sunxi_wlan_dev = {
-	.minor = MISC_DYNAMIC_MINOR,
+	.minor = 200,
 	.name  = "sunxi-wlan",
 };
```

修改后的最终内容(直接复制):

```c
static struct miscdevice sunxi_wlan_dev = {
	.minor = 200,        /* /proc/misc 里 200 空闲 */
	.name  = "sunxi-wlan",
 };
```

#### 内核 Menuconfig 配置

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4
```

修改内容(约 99 行,上下文 `CONFIG_NETDEVICES=y / CONFIG_SUNXI_GMAC=y / CONFIG_USB_USBNET=y`),diff:

```diff
 CONFIG_USB_USBNET=y
-CONFIG_XR829_WLAN=m
+CONFIG_RTL8723DS=m
```

修改后的最终内容(直接复制):

```shell
CONFIG_RTL8723DS=m                     /* 换成 RTL8723DS(XR829 是 XR 系模组) */
```

#### 内核设备树配置

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts
```

修改内容 —— `rfkill@0` 节点(启用 rfkill;wlan 子节点按"常供电模块"精简),diff:

```diff
 	rfkill: rfkill@0 {
 		compatible    = "allwinner,sunxi-rfkill";
 		chip_en;
 		power_en;
-		pinctrl-0 = <&wlan_pins_a>;
-		pinctrl-names = "default";
-		status        = "disabled";
-
-		wlan: wlan@0 {
-			compatible    = "allwinner,sunxi-wlan";
-			clock-names = "32k-fanout1";
-			clocks = <&ccu CLK_FANOUT1_OUT>;
-			wlan_busnum    = <0x1>;
-			wlan_regon    = <&pio PB 7 GPIO_ACTIVE_HIGH>;
-			wlan_hostwake  = <&pio PG 10 GPIO_ACTIVE_HIGH>;
-			/*wlan_power    = "VCC-3V3";*/
-			/*wlan_power_vol = <3300000>;*/
-		};
+		status        = "okay";
+	};
```

`rfkill@0` 节点的最终内容(直接复制,整体替换原来的 `rfkill: rfkill@0 { ... };`):

```shell
	rfkill: rfkill@0 {
		compatible    = "allwinner,sunxi-rfkill";
		chip_en;
		power_en;
		status        = "okay";
		/* 注意:不要配 pinctrl-0!它会对 pinctrl 驱动产生 devlink 依赖,
		 * probe 可能无限 defer(设备永远不 attach)。本板 PG11(32K)未接线。 */

        wlan: wlan@0 {
            compatible    = "allwinner,sunxi-wlan";
            wlan_busnum    = <0x1>;        // SDIO bus 1 (mmc1)
            /* RTL8723DS 模块常供电(WL_DIS#/BT_DIS# 拉高,不接 MCU):
             * 不配 wlan_regon / wlan_hostwake GPIO;也不配 32k-fanout1 时钟
             * (模块内部自带 32K,配了会产生 ccu devlink 依赖导致 probe defer)。 */
        };

        /* 注意:没有 "sunxi-bt"(bt@0)节点。蓝牙完全由用户态 rtk_hciattach
         * (rtk_h5) + 内核 hci_rtk_h5 传输驱动负责;sunxi-bt 平台驱动会去占用
         * PB6(bt_rst_n),而 PB6 已被 uart3 用,会导致 rfkill probe 整体失败。 */
	};
```

#### OpenWrt 配置(kmod 包 + 固件)

第二次给 defconfig 加包(触摸章加过 evtest/i2c-tools,这次加 WiFi 相关的)。

##### 添加 kmod 包定义(无 AUTOLOAD!)

```shell
vi <sdk>/openwrt/target/t113/t113-common/modules.mk
```

修改内容(在 `$(eval $(call KernelPackage,net-rtl8821cs))` 之后插入),diff:

```diff
 $(eval $(call KernelPackage,net-rtl8821cs))
+
+define KernelPackage/net-rtl8723ds
+  SUBMENU:=$(WIRELESS_MENU)
+  TITLE:=RTL8723DS support
+  DEPENDS:= +@IPV6 +@USES_REALTEK
+  FILES:=$(LINUX_DIR)/drivers/net/wireless/rtl8723ds/8723ds.ko
+  # No AUTOLOAD: the RTL8723DS WiFi SDIO firmware download would corrupt the
+  # module's BT ROM-download state. BT (rtk_hciattach) must run FIRST, then
+  # 8723ds is loaded explicitly by /etc/rc.local (base-files) once hci0 is up.
+endef
+
+define KernelPackage/net-rtl8723ds/description
+ Kernel modules for RealTek RTL8723DS support
+endef
+
+$(eval $(call KernelPackage,net-rtl8723ds))
```

追加的内容(直接复制,插入在 `$(eval $(call KernelPackage,net-rtl8821cs))` 之后):

```shell
define KernelPackage/net-rtl8723ds
  SUBMENU:=$(WIRELESS_MENU)
  TITLE:=RTL8723DS support
  DEPENDS:= +@IPV6 +@USES_REALTEK
  FILES:=$(LINUX_DIR)/drivers/net/wireless/rtl8723ds/8723ds.ko
  # No AUTOLOAD: the RTL8723DS WiFi SDIO firmware download would corrupt the
  # module's BT ROM-download state. BT (rtk_hciattach) must run FIRST, then
  # 8723ds is loaded explicitly by /etc/rc.local (base-files) once hci0 is up.
endef

define KernelPackage/net-rtl8723ds/description
 Kernel modules for RealTek RTL8723DS support
endef

$(eval $(call KernelPackage,net-rtl8723ds))
```

> **关键设计:故意不写 `AUTOLOAD` 行。** 8723ds 的 WiFi SDIO 固件下载会破坏模块 BT 侧
> 的下载状态:WiFi 先于蓝牙初始化的话,之后 `rtk_hciattach` 会一直 H5 sync timeout。
> 开机顺序必须是 **BT 先起(rtk_hciattach)→ 再加载 8723ds**,由 `/etc/rc.local` 控制(见蓝牙章)。

##### 修改 defconfig(包选择)

```shell
vi <sdk>/openwrt/target/t113/t113-zgl_board/defconfig
```

修改内容(5 处),diff:

```diff
-# CONFIG_XR829_USE_40M_SDD is not set
-CONFIG_USES_XR829=y
+CONFIG_USES_REALTEK=y

-# CONFIG_PACKAGE_rtl8723ds-firmware is not set
+CONFIG_PACKAGE_rtl8723ds-firmware=y

-CONFIG_PACKAGE_xr829-firmware=y
+# CONFIG_PACKAGE_xr829-firmware is not set

-CONFIG_PACKAGE_kmod-net-xr829=y
+# CONFIG_PACKAGE_kmod-net-xr829 is not set
+CONFIG_PACKAGE_kmod-net-rtl8723ds=y

-CONFIG_PACKAGE_xr829-rftest=y
+# CONFIG_PACKAGE_xr829-rftest is not set
```

修改后的最终内容(直接复制,替换对应的行):

```shell
CONFIG_USES_REALTEK=y                  /* 位置在 defconfig 约 3195 行(替换 USES_XR829 那两行) */
```

```shell
CONFIG_PACKAGE_rtl8723ds-firmware=y    /* 约 3276 行(装蓝牙 H4 固件 /lib/firmware/rtlbt/) */
```

```shell
# CONFIG_PACKAGE_xr829-firmware is not set        /* 约 3293 行(替换 CONFIG_PACKAGE_xr829-firmware=y) */
# CONFIG_PACKAGE_kmod-net-xr829 is not set        /* 约 4075 行 */
CONFIG_PACKAGE_kmod-net-rtl8723ds=y               /* 新增,在上一行之后 */
```

```shell
# CONFIG_PACKAGE_xr829-rftest is not set          /* 约 6985 行(替换 CONFIG_PACKAGE_xr829-rftest=y) */
```

> `rtl8723ds-firmware` 的源文件在 `platform/allwinner/wireless/firmware/rtl8723ds/`
> (rtl8723d_fw/rtl8723d_config 是 WiFi 固件(内嵌 8723ds.ko 也带一份);rtl8723dsh4_fw/
> rtl8723dsh4_config 是蓝牙 H4 固件,蓝牙章的 rtk_hciattach 用)。

#### 重新编译打包

```shell
./build.sh kernel -j8
./build.sh openwrt_rootfs -j1
./build.sh pack
```

> 烧录前可在主机自检 rootfs:
> `unsquashfs -l out/t113/zgl_board/openwrt/rootfs.img | grep -E 'rtlbt|8723ds.ko'`
> 应看到 `lib/modules/5.4.61/8723ds.ko` 和 `lib/firmware/rtlbt/` 下 4 个固件文件。

#### 测试

> ⚠️ 换固件后建议**彻底断电 ≥15 秒再上电**(模块常供电,flash 状态残留可能影响 SDIO 枚举)。

```shell
# 1. 开机自动部分(无需手动)
adb shell 'cat /proc/misc | grep wlan'
# 预期: 200 sunxi-wlan            ← 静态 minor 200 生效

adb shell 'ls /sys/bus/platform/drivers/ | grep sunxi-rfkill'
# 预期: sunxi-rfkill

# 2. 手动加载 WiFi 模块(蓝牙章的 rc.local 时序好了之后由开机自动)
adb shell insmod /lib/modules/5.4.61/8723ds.ko
adb shell lsmod
# 预期: 8723ds  2150400  0

# 3. 扫描
adb shell 'ip link set wlan0 up && iw dev wlan0 scan | grep SSID | head -10'
# 预期: 扫到周围 AP

# 4. 连接热点 → DHCP
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
# 预期: wpa_state=COMPLETED  ssid=你的热点名  ip_address=x.x.x.x

adb shell 'udhcpc -i wlan0 -n -q'
# 预期: lease of x.x.x.x obtained

# 5. ping 验证
adb shell 'ping -c 3 网关IP'     # 0% packet loss
adb shell 'ping -c 3 223.5.5.5'  # 0% packet loss
```

> **SDIO 枚举判据说明(实测 2026-10-20)**:旧文档用 `dmesg | grep mmc1` 找
> `new high speed SDIO card`,但本镜像实测该行**不出现在 dmesg**(mmc core 的枚举
> 打印发生在 8723ds probe 期间且级别/时机原因不落缓冲区)。可靠判据是
> 8723ds probe 期间的 RTW 系列(8723ds 加载后约 0.2 秒):
>
> ```
> [   12.347663] RTW: rtl8723ds v5.10.1-33-g2b795d825.20210322_beta
> [   12.359534] RTW: rtl8723ds BT-Coex version = COEX20200103-3535
> [   12.366128] sunxi-rfkill soc@3000000:rfkill@0: wlan power on success
> [   12.473307] sunxi-rfkill soc@3000000:rfkill@0: bus_index: 1
> [   12.490915] RTW: == SDIO Card Info ==
> [   12.523463] RTW: CHIP TYPE: RTL8723D
> [   12.563986] RTW: sdio_power_on_check: 0x1B8 test Pass.
> [   12.679335] RTW: rtl8723d_FirmwareDownload fw: FW_NIC, size: 29262
> [   13.227940] RTW: rtw_ndev_init(wlan0) if1 mac_addr=34:75:63:28:4d:23
> ```
>
> `adb shell 'dmesg | grep -E "RTW: CHIP TYPE|rtw_ndev_init"'` 有输出即 SDIO 枚举
> 成功、驱动 probe 完成。

**实测记录(zgl 热点,WPA2-PSK,2026-10-20)**:

```text
$ adb shell 'wpa_cli -i wlan0 status'
bssid=62:e4:47:9d:d6:12
freq=2412
ssid=zgl
id=0
mode=station
wifi_generation=4
pairwise_cipher=CCMP
group_cipher=CCMP
key_mgmt=WPA2-PSK
wpa_state=COMPLETED          ← 连接完成
address=34:75:63:28:4d:23

$ adb shell 'udhcpc -i wlan0 -n -q'
udhcpc: lease of 10.25.28.164 obtained, lease time 3599
udhcpc: ip addr add 10.25.28.164/255.255.255.0 broadcast 10.25.28.255 dev wlan0
udhcpc: setting default routers: 10.25.28.129

$ adb shell 'ping -c 3 10.25.28.129'
3 packets transmitted, 3 packets received, 0% packet loss
$ adb shell 'ping -c 3 223.5.5.5'
3 packets transmitted, 3 packets received, 0% packet loss

$ adb shell 'iw dev wlan0 link'
Connected to 62:e4:47:9d:d6:12 (on wlan0)
    SSID: zgl
    freq: 2412
    signal: -42 dBm            ← 信号强度
    tx bitrate: 72.2 MBit/s    ← 速率
```

<!-- 截图: wpa_cli status COMPLETED + ping -->

##### WiFi 排错速查

| 现象 | 原因 | 处理 |
|---|---|---|
| `get wifi_sdc_id failed` | rfkill probe 永久注销 | sunxi-rfkill.c 换普通注册 |
| `/dev/sunxi-wlan` 不存在 | misc minor -EBUSY | sunxi-wlan.c 静态 200 |
| 4-way 握手循环失败 `WRONG_KEY` | **密码不对**,不是驱动 | `wpa_supplicant -dd` 看日志确认 |
| rfkill/wlan 永远 probe 不上 | pinctrl 依赖 defer | rfkill@0 不配 pinctrl-0 |

### 蓝牙(RTL8723DS,H5 / UART1 四线)

原理图:BT HCI 走 UART1 四线(PG6 TX→模块 UART_IN、PG7 RX←UART_OUT、PG8 RTS→模块 CTS、PG9 CTS←模块 RTS);BT_DIS#(Pin34)R52 10kΩ 上拉(常使能,无 MCU 复位);SUSCLK_IN(Pin24)R91 10kΩ 上拉(RTL8723DS 需要,BS 不需要)。115200 起步,固件下载后切 1.5M。

#### 内核 Menuconfig 配置

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4
```

修改内容(约 77 行,上下文 `CONFIG_BT=y / CONFIG_BT_RFCOMM=y / CONFIG_BT_RFCOMM_TTY=y / CONFIG_BT_HCIUART=y / CONFIG_BT_HCIUART_H4=y`),diff:

```diff
 CONFIG_BT_HCIUART=y
 CONFIG_BT_HCIUART_H4=y
+CONFIG_BT_HCIUART_RTL3WIRE=y
-CONFIG_XR_BT_LPM=y
+# CONFIG_XR_BT_LPM is not set
```

修改后的最终内容(直接复制):

```shell
CONFIG_BT_HCIUART=y
CONFIG_BT_HCIUART_H4=y
CONFIG_BT_HCIUART_RTL3WIRE=y           /* Realtek H5 三线协议(drivers/bluetooth/Kconfig) */
# CONFIG_XR_BT_LPM is not set          /* XR 系专用,RTL8723DS 不用 */
```

#### 内核设备树配置

```shell
vi <sdk>/device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts
```

修改内容 1 —— `&uart1` 使能(BT HCI 串口),diff:

```diff
 &uart1 {
 	pinctrl-names = "default", "sleep";
 	pinctrl-0 = <&uart1_pins_a>;
 	pinctrl-1 = <&uart1_pins_b>;
-	status = "disabled";
+	status = "okay";
 };
```

`&uart1` 节点的最终内容(直接复制,整体替换原来的 `&uart1 { ... };`):

```shell
&uart1 {
	pinctrl-names = "default", "sleep";
	pinctrl-0 = <&uart1_pins_a>;
	pinctrl-1 = <&uart1_pins_b>;
	status = "okay";           /* RTL8723DS BT UART (4-wire, 115200) */
};
```

修改内容 2 —— pinctrl 组改成四线(evb1 模板只有 TX/RX 两线),diff:

```diff
-	uart1_pins_a: uart1_pins@0 {  /* For EVB1 board */
-		pins = "PG6", "PG7";
+	uart1_pins_a: uart1_pins@0 {  /* For BT (RTL8723DS) 4-wire: TX/RX/RTS/CTS */
+		pins = "PG6", "PG7", "PG8", "PG9";
 		function = "uart1";
 		drive-strength = <10>;
 		bias-pull-up;
 	};
 
 	uart1_pins_b: uart1_pins@1 {
-		pins = "PG6", "PG7";
+		pins = "PG6", "PG7", "PG8", "PG9";
 		function = "gpio_in";
 	};
```

pinctrl 组的最终内容(直接复制):

```shell
	uart1_pins_a: uart1_pins@0 {  /* For BT (RTL8723DS) 4-wire: TX/RX/RTS/CTS */
		pins = "PG6", "PG7", "PG8", "PG9";
		function = "uart1";
		drive-strength = <10>;
		bias-pull-up;
	};

	uart1_pins_b: uart1_pins@1 {
		pins = "PG6", "PG7", "PG8", "PG9";
		function = "gpio_in";
	};
```

> **注意:不要加 `sunxi-bt`(bt@0)节点**(原厂 R329+RTL8723DS 指南里有的那个)。
> 本板没有复位脚,加了这个节点它会去占 PB6(bt_rst_n),与 uart3 冲突,导致 rfkill probe 整体失败。

#### 移植 rtk_hciattach(锁版二进制)

SDK 自带的 rtk_hciattach 源码是 2017 plow 版,缺 `rtk_parse_config_file`/`h4_patch_table` 等,**会导致 inquiry scan + SSP 配对失败(手机搜不到设备)**;更新版源码找不到,所以直接用锁版二进制(100096 字节,起 hci0 后 ACL MTU 1021 是正确版本的标志)。

```shell
# 建目录(整包从参考包复制:rtk_hciattach prebuilt + src)
cd <sdk>/platform/thirdparty
mkdir -p wireless/rtk_hciattach/prebuilt wireless/rtk_hciattach/src
cp <参考包>/files/platform/thirdparty/wireless/rtk_hciattach/prebuilt/rtk_hciattach wireless/rtk_hciattach/prebuilt/
cp <参考包>/files/platform/thirdparty/wireless/rtk_hciattach/src/* wireless/rtk_hciattach/src/
```

锁版二进制校验:

```shell
md5sum wireless/rtk_hciattach/prebuilt/rtk_hciattach
# 预期: ed6f599f3a8f9ceda282b9f221b2afab  (100096 字节)
```

##### 修改 OpenWrt 包 Makefile(编译源码 → 拷 prebuilt)

```shell
vi <sdk>/openwrt/package/feeds/utils/rtk_hciattach/Makefile
```

修改内容(`define Build/Compile` 里把编译块整体替换为拷贝),diff:

```diff
 define Build/Compile
-	$(MAKE) -C $(PKG_BUILD_DIR)/src/ \
-	ARCH="$(TARGET_ARCH)" \
-	AR="$(TARGET_AR)" \
-	CC="$(TARGET_CC)" \
-	CXX="$(TARGET_CXX)" \
-	CFLAGS="$(TARGET_CFLAGS)" \
-	LDFLAGS="$(TARGET_LDFLAGS)" \
-	CONFIG_PREFIX="$(PKG_INSTALL_DIR)" \
-	all
+	mkdir -p $(PKG_INSTALL_DIR)
+	$(CP) $(SRC_CODE_DIR)/prebuilt/rtk_hciattach $(PKG_INSTALL_DIR)/
 endef
```

修改后的 `Build/Compile`(直接复制,整体替换):

```shell
define Build/Compile
	# 用 prebuilt 更新版 rtk_hciattach（100KB，Allwinner btmanager 4.0 配套，含
	# rtk_parse_config_file/h4_patch_table 等 plow 2017 版缺失的功能，ACL MTU 1021）。
	# plow 2017 版会导致 inquiry scan + SSP 配对失败，源码又找不到，故直接用 prebuilt。
	mkdir -p $(PKG_INSTALL_DIR)
	$(CP) $(SRC_CODE_DIR)/prebuilt/rtk_hciattach $(PKG_INSTALL_DIR)/
endef
```

#### 移植 btmanager 4.0.3(锁版二进制)

SDK 的 btmanager 源码是 4.0.5,它的 `bt_manager_enable()` 不会调 `bt_adapter_set_power()`,adapter ON 回调不触发 → agent IO 能力保持 DisplayOnly → 配对走 Numeric Comparison **要 PIN**;4.0.3 有 `bt_adapter_set_power`,回调正常 → **免 PIN 配对**。同样用锁版二进制。

```shell
cd <sdk>/platform/allwinner/wireless/btmanager
mkdir -p prebuilt
cp <参考包>/files/platform/allwinner/wireless/btmanager/prebuilt/libbtmg.so prebuilt/
cp <参考包>/files/platform/allwinner/wireless/btmanager/prebuilt/bt_test prebuilt/
```

锁版二进制校验:

```shell
md5sum prebuilt/*
# 预期:
# 2b8d26e3811a591773b8686322261dd5  libbtmg.so  (285460 字节,btmanager 4.0.3)
# 32fe836aa7493f95468b53699617f741  bt_test     (158268 字节)
```

##### 修改 bt_init.sh(补 hci0 up)

```shell
vi <sdk>/platform/allwinner/wireless/btmanager/config/bt_init.sh
```

修改内容(`start()` 函数里,`bluetoothd start` 之后插入),diff:

```diff
 	d=`ps | grep bluetoothd | grep -v grep`
 	[ -z "$d" ] && {
 		# bluetoothd -n &
 		/etc/bluetooth/bluetoothd start
 		sleep 1
 	}
 
+	# power on the adapter (hciconfig up). btmanager 4.0.5's bt_manager_enable()
+	# does NOT call bt_adapter_set_power() (unlike 4.0.3), so without this the
+	# adapter stays DOWN, bt_adapter_get_power_state() returns OFF, and the
+	# BTMG_ADAPTER_ON callback (which sets io capability NoInputNoOutput) never
+	# fires -> pairing falls back to Numeric Comparison (asks PIN).
+	hci_is_up=`hciconfig hci0 | grep UP`
+	[ -z "$hci_is_up" ] && {
+		hciconfig hci0 up
+	}
 }
```

插入的代码块(直接复制,插入在 `bluetoothd start` 的 `}` 之后):

```shell
	# power on the adapter (hciconfig up). btmanager 4.0.5's bt_manager_enable()
	# does NOT call bt_adapter_set_power() (unlike 4.0.3), so without this the
	# adapter stays DOWN, bt_adapter_get_power_state() returns OFF, and the
	# BTMG_ADAPTER_ON callback (which sets io capability NoInputNoOutput) never
	# fires -> pairing falls back to Numeric Comparison (asks PIN).
	hci_is_up=`hciconfig hci0 | grep UP`
	[ -z "$hci_is_up" ] && {
		hciconfig hci0 up
	}
```

##### 修改 btmanager 包 Makefile(4.0.5 源码编译 → 4.0.3 prebuilt)

```shell
vi <sdk>/openwrt/package/allwinner/wireless/btmanager/Makefile
```

修改内容 1 —— DEPENDS 补全(4.0.3 prebuilt bt_test 直接链 libdbus/libgio/libglib/libgobject/libsbc/libncursesw;原条件依赖 `+ENABLE_CLASSIC_BLUETOOTH:dbus` 等不会触发依赖检查,libsbc 原本就缺 → 全部改为无条件),diff:

```diff
 define Package/$(PKG_NAME)
   $(call Package/$(PKG_NAME)/Default)
   TITLE:=bluetooth manager core
-  DEPENDS:=+libjson-c +wirelesscommon +bluez-libs +bluez-utils +alsa-lib +libreadline +libncurses\
-           +ENABLE_CLASSIC_BLUETOOTH:bluez-daemon \
-           +ENABLE_CLASSIC_BLUETOOTH:dbus \
-           +ENABLE_CLASSIC_BLUETOOTH:glib2 \
-           +BTMG_ENABLE_MESH:ell
+  DEPENDS:=+libjson-c +wirelesscommon +bluez-libs +bluez-utils +alsa-lib +libreadline +libncurses +libncursesw +sbc +dbus +glib2 +bluez-daemon
 endef
```

修改后的最终内容(直接复制,替换 `define Package/$(PKG_NAME)` 里的 DEPENDS 行):

```shell
  # 4.0.3 prebuilt bt_test 直接链 libdbus/libgio/libglib/libgobject/libsbc/libncursesw，
  # openwrt 依赖检查要求这些 provider 全部出现在 DEPENDS 闭包里（原条件依赖
  # +ENABLE_CLASSIC_BLUETOOTH:dbus 等不会触发依赖检查，libsbc 原本就缺）→ 全部改为无条件。
  DEPENDS:=+libjson-c +wirelesscommon +bluez-libs +bluez-utils +alsa-lib +libreadline +libncurses +libncursesw +sbc +dbus +glib2 +bluez-daemon
```

修改内容 2 —— `Build/Compile` 里把源码编译块整体替换为 prebuilt 拷贝(约 114 行),diff:

```diff
 define Build/Compile/$(PKG_NAME)
-	$(MAKE) -C $(PKG_BUILD_DIR)/src/ \
-		PKG_BUILD_DIR="$(PKG_BUILD_DIR)" \
-		ARCH="$(TARGET_ARCH)" \
-		AR="$(TARGET_AR)" \
-		CC="$(TARGET_CC)" \
-		CXX="$(TARGET_CXX)" \
-		CFLAGS="$(TARGET_CFLAGS)" \
-		BT_EXT_CFLAG="$(BT_EXT_CFLAG)" \
-		LDFLAGS="$(TARGET_LDFLAGS)" \
-		STAGING_DIR="$(STAGING_DIR)" \
-		CONFIG_LIBC="$(CONFIG_LIBC)$(if $(CONFIG_EXT_TOOLCHAIN_GCC_8_3_0),-gcc8_3_0)" \
-		CPU="$(ARCH)" \
-		CONFIG_PREFIX="$(PKG_INSTALL_DIR)" \
-		$(LOCAL_FLAGS) \
-		all
-ifeq ($(CONFIG_BTMG_ENABLE_DEMO), y)
-	$(MAKE) -C $(PKG_BUILD_DIR)/demo/ \
-		... (demo/mesh 分支同样删除)
-endif
+	mkdir -p $(PKG_INSTALL_DIR)/usr/lib $(PKG_INSTALL_DIR)/usr/bin $(PKG_INSTALL_DIR)/usr/include
+	$(CP) $(SRC_CODE_DIR)/prebuilt/libbtmg.so $(PKG_INSTALL_DIR)/usr/lib/
+	$(CP) $(SRC_CODE_DIR)/prebuilt/bt_test $(PKG_INSTALL_DIR)/usr/bin/
+	$(CP) $(SRC_CODE_DIR)/src/include/*.h $(PKG_INSTALL_DIR)/usr/include/
 endef
```

修改后的 `Build/Compile/$(PKG_NAME)`(直接复制,整体替换):

```shell
define Build/Compile/$(PKG_NAME)
	# 用 4.0.3 prebuilt（libbtmg.so 285460B + bt_test 158268B）替代 SDK 4.0.5 源码编译。
	# 4.0.5 的 bt_manager_enable() 中 adapter ON 回调不触发 → agent IO 能力保持
	# DisplayOnly → 配对走 Numeric Comparison 要 PIN；4.0.3 有 bt_adapter_set_power，
	# 回调正常 → 免 PIN 配对。详见 docs/rtl8723ds-bluetooth-porting.md §3.5。
	# 头文件仍取自 src/include（InstallDev 需要）；bt_init.sh/bluetooth.json 仍取自 config/。
	mkdir -p $(PKG_INSTALL_DIR)/usr/lib $(PKG_INSTALL_DIR)/usr/bin $(PKG_INSTALL_DIR)/usr/include
	$(CP) $(SRC_CODE_DIR)/prebuilt/libbtmg.so $(PKG_INSTALL_DIR)/usr/lib/
	$(CP) $(SRC_CODE_DIR)/prebuilt/bt_test $(PKG_INSTALL_DIR)/usr/bin/
	$(CP) $(SRC_CODE_DIR)/src/include/*.h $(PKG_INSTALL_DIR)/usr/include/
endef
```

##### 修改 defconfig(启用 rtk_hciattach)

第三次给 defconfig 加包(这次加蓝牙的 rtk_hciattach):

```shell
vi <sdk>/openwrt/target/t113/t113-zgl_board/defconfig
```

修改内容(约 6991 行),diff:

```diff
 # rtk_hciattach
 #
-# CONFIG_PACKAGE_rtk_hciattach is not set
+CONFIG_PACKAGE_rtk_hciattach=y
 # end of rtk_hciattach
```

修改后的最终内容(直接复制):

```shell
# rtk_hciattach
#
CONFIG_PACKAGE_rtk_hciattach=y
# end of rtk_hciattach
```

> btmanager 相关包(`btmanager-v4.0=y`、`BTMG_ENABLE_TTYS1=y`、`BTMG_ENABLE_DEMO=y`、
> `bluez-libs=y` 等)模板 defconfig 已有,不用改。

#### 修改 rc.local(BT 先 WiFi 后的开机时序)

**整个移植最容易错的地方**。原因:8723ds 的 WiFi SDIO 固件下载会破坏模块 BT 侧的下载状态;模块常供电、无复位脚,坏状态只能整板断电清。所以开机必须 **BT 先 attach(下载 BT 固件)→ WiFi 模块后加载**。

```shell
vi <sdk>/openwrt/target/t113/t113-zgl_board/base-files/etc/rc.local
```

修改内容(在音频 amixer 行之后、`exit 0` 之前插入),diff:

```diff
 # capture
 amixer -Dhw:audiocodec cset name='MIC3 Input Select' 1
 amixer -Dhw:audiocodec cset name='MIC3 Switch' 1
 amixer -Dhw:audiocodec cset name='ADC3 Gain' 19
 
+# RTL8723DS bring-up: BT FIRST, then WiFi.
+# The WiFi SDIO firmware download corrupts the module's BT ROM-download state,
+# and the module is always-powered with no reset GPIO, so BT (rtk_hciattach
+# downloads BT firmware over UART1) must run before 8723ds is loaded. The
+# 8723ds.ko is installed by kmod-net-rtl8723ds but intentionally NOT autoloaded.
+
+# Diagnostic: log whether 8723ds is already loaded and run BT in foreground
+# with full logging, waiting up to 30s for hci0.
+{
+	echo "=== rtl_bt_wifi: 8723ds loaded? ==="
+	lsmod | grep -c 8723ds || true
+	echo "=== rtl_bt_wifi: starting rtk_hciattach ==="
+	rtk_hciattach -n -s 115200 /dev/ttyS1 rtk_h5
+} > /tmp/bt_boot.log 2>&1 &
+BT_PID=$!
+
+for i in $(seq 1 30); do
+	[ -d /sys/class/bluetooth/hci0 ] && { echo "BT up after ${i}s" >> /tmp/bt_boot.log; break; }
+	sleep 1
+done
+
+# WiFi AFTER BT is up: the WiFi SDIO firmware download would otherwise corrupt
+# the module's BT ROM-download state (module is always-powered, no reset GPIO,
+# so a bad download state can only be cleared by a full power cycle).
+if [ -d /sys/class/bluetooth/hci0 ]; then
+	modprobe 8723ds && echo "=== rtl_bt_wifi: 8723ds loaded (after BT) ===" >> /tmp/bt_boot.log
+else
+	echo "=== rtl_bt_wifi: WARNING hci0 not up, 8723ds NOT loaded; full power-cycle required ===" >> /tmp/bt_boot.log
+fi
+
 exit 0
```

`rc.local` 的最终内容(直接复制,整体替换原来的文件):

```shell
# Put your custom commands here that should be executed once
# the system init finished. By default this file does nothing.

amixer cset name="HPOUT Switch" 1

# capture
amixer -Dhw:audiocodec cset name='MIC3 Input Select' 1
amixer -Dhw:audiocodec cset name='MIC3 Switch' 1
amixer -Dhw:audiocodec cset name='ADC3 Gain' 19

# RTL8723DS bring-up: BT FIRST, then WiFi.
# The WiFi SDIO firmware download corrupts the module's BT ROM-download state,
# and the module is always-powered with no reset GPIO, so BT (rtk_hciattach
# downloads BT firmware over UART1) must run before 8723ds is loaded. The
# 8723ds.ko is installed by kmod-net-rtl8723ds but intentionally NOT autoloaded.

# Diagnostic: log whether 8723ds is already loaded and run BT in foreground
# with full logging, waiting up to 30s for hci0.
{
	echo "=== rtl_bt_wifi: 8723ds loaded? ==="
	lsmod | grep -c 8723ds || true
	echo "=== rtl_bt_wifi: starting rtk_hciattach ==="
	rtk_hciattach -n -s 115200 /dev/ttyS1 rtk_h5
} > /tmp/bt_boot.log 2>&1 &
BT_PID=$!

for i in $(seq 1 30); do
	[ -d /sys/class/bluetooth/hci0 ] && { echo "BT up after ${i}s" >> /tmp/bt_boot.log; break; }
	sleep 1
done

# WiFi AFTER BT is up: the WiFi SDIO firmware download would otherwise corrupt
# the module's BT ROM-download state (module is always-powered, no reset GPIO,
# so a bad download state can only be cleared by a full power cycle).
if [ -d /sys/class/bluetooth/hci0 ]; then
	modprobe 8723ds && echo "=== rtl_bt_wifi: 8723ds loaded (after BT) ===" >> /tmp/bt_boot.log
else
	echo "=== rtl_bt_wifi: WARNING hci0 not up, 8723ds NOT loaded; full power-cycle required ===" >> /tmp/bt_boot.log
fi

exit 0
```

#### 重新编译打包

```shell
./build.sh kernel -j8
./build.sh openwrt_rootfs -j1
./build.sh pack
```

#### 测试

> ⚠️ **烧录后必须彻底断电(拔掉所有线,包括 adb USB!)30 秒再上电**——原理图上调试/供电
> Type-C 都直接喂 3V3 轨,模块 VBAT 常带电,只断电源适配器等于没断电(见排错速查)。

##### 1. 开机时序验证(rc.local 自动)

```shell
adb shell cat /tmp/bt_boot.log
```

成功日志(**实测原文 2026-10-20**):

```
=== rtl_bt_wifi: 8723ds loaded? ===
0
=== rtl_bt_wifi: starting rtk_hciattach ===
Realtek Bluetooth init uart with init speed:115200, final_speed:115200, type:HCI UART H5
Realtek Bluetooth :Realtek hciattach version 3.1
Realtek Bluetooth :3-wire sync pattern resend : 1, len: 8
Realtek Bluetooth :Get SYNC Resp Pkt
Realtek Bluetooth :Get SYNC pkt-active mode
Realtek Bluetooth :3-wire config pattern resend : 1 , len: 10
Realtek Bluetooth :Get CONFG pkt-active mode
Realtek Bluetooth :Get CONFG resp pkt-active mode
Realtek Bluetooth :H5 init finished
Realtek Bluetooth :H5 IC
Realtek Bluetooth :RTK send HCI_VENDOR_READ_RTK_ROM_VERISION_Command
Realtek Bluetooth :Read Local Version Information with Status:0
Realtek Bluetooth :HCI Version 0x08          ← ROM 状态(BT4.2)
Realtek Bluetooth :HCI Revision 0x000d
Realtek Bluetooth :LMP Subversion 0x8723     ← ROM 干净态标志(被污染时是 0x6f72 + IC: NONE)
Realtek Bluetooth :Read RTK rom version with Status:0
Realtek Bluetooth :LMP Subversion 0x8723
Realtek Bluetooth :EVersion 2
Realtek Bluetooth :IC: RTL8723DS
Realtek Bluetooth :Firmware/config: rtl8723d_fw, rtl8723d_config
Realtek Bluetooth :Couldnt access customer BT MAC file /opt/bdaddr   ← 正常,无 /opt/bdaddr 时用默认 MAC
Realtek Bluetooth :Original Cfg len 41
Realtek Bluetooth :Config baud : 04928002    ← 固件下载后切 1.5M
Realtek Bluetooth :hw flow control: 1
Realtek Bluetooth :Load FW /lib/firmware/rtlbt/rtl8723d_fw OK
Realtek Bluetooth :Init Process finished
BT up after 2s                                ← rc.local 打点
=== rtl_bt_wifi: 8723ds loaded (after BT) === ← WiFi 在 BT 之后
```

##### 2. 拉起 A2DP Sink(重启后每次需要,rc.local 只保证 hci0)

```shell
adb shell '/sbin/start-stop-daemon -b -m -S -p /tmp/bt_test.pid -x /usr/bin/bt_test -- a2dp-sink'
```

> 用 `start-stop-daemon` 而不是直接跑 `bt_test`,避免 kill_test_process 误判杀进程。

```shell
adb shell hciconfig hci0
# 预期(**实测 2026-10-20**):
# hci0:	Type: Primary  Bus: UART
# 	BD Address: 34:75:63:46:D1:A3  ACL MTU: 1021:8  SCO MTU: 255:12   ← 1021 = 锁版特征
# 	UP RUNNING PSCAN ISCAN                                            ← 可连接+可发现
# 	RX bytes:2473 acl:0 sco:0 events:75 errors:0
# 	TX bytes:3097 acl:0 sco:0 commands:76 errors:0
adb shell hciconfig hci0 -a | grep -E "Name|Class"
# 预期(**实测**):
# 	Name: 'aw-bt-test-D1-A3'                                          ← 后缀 = MAC 后两字节
# 	Class: 0x040400                                                   ← btmanager 4.0.3 回调标志
# 	Service Classes: Rendering
# 	Device Class: Audio/Video, Uncategorized
```

##### 3. 手机实测

1. 手机蓝牙搜索 → 看到 **`aw-bt-test-XX-XX`**(音频设备)
2. 点击配对 → **不弹 PIN,直接配对成功**(btmanager 4.0.3 免 PIN)
3. 媒体音频里连上 → 放歌 → **喇叭出声、音质正常**
4. 音频流证据(实测 2026-10-20):

```shell
# 音频流在跑(playing 时连抓两次,hw_ptr 持续增长):
$ adb shell 'cat /proc/asound/card0/pcm0p/sub0/status'
state: RUNNING                ← A2DP 音频流经 codec 输出
owner_pid   : 2540            ← bt_test 的 bluealsa/a2dp 线程
hw_ptr      : 2113344
(3 秒后再抓:)
hw_ptr      : 2265728         ← 持续增长 = 正在出声

# 蓝牙链路(RX acl 包持续增长, 3 秒 3507→3723→5141):
$ adb shell 'hciconfig hci0'
	RX bytes:2101591 acl:3507 sco:0 events:278 errors:0
	TX bytes:38341 acl:147 sco:0 commands:103 errors:0
(3 秒后:)
	RX bytes:2234029 acl:3723 sco:0 events:281 errors:0   ← acl 包在涨 = 手机在推音乐数据

# 连接状态(手机已配对加密):
$ adb shell 'hcitool con'
Connections:
	> ACL 94:F6:F2:C7:54:C9 handle 2 state 1 lm MASTER AUTH ENCRYPT
	                                    ↑ 手机侧地址  ↑ 已认证+已加密

# 编解码参数(A2DP 下发到 codec 的实际格式):
$ adb shell 'cat /proc/asound/card0/pcm0p/sub0/hw_params'
access: MMAP_INTERLEAVED
format: S16_LE
channels: 2
rate: 48000 (48000/1)         ← A2DP 标准 48kHz
```

<!-- 截图: 手机蓝牙列表 + hciconfig -->

##### 蓝牙排错速查(全部实测踩出来的)

| 现象 | 原因 | 处理 |
|---|---|---|
| `H5 sync timed out` | 模块 BT 状态被污染(WiFi 固件下载破坏) | **整板断电**:拔掉所有线(含 adb USB)30 秒再上电 |
| 手机搜不到设备 | hciattach 版本不对 | 锁版 100096B;验证:`hciconfig hci0` 的 **ACL MTU 1021** |
| 配对要 PIN | btmanager 版本不对 | 锁版 4.0.3(libbtmg 285460B) |
| 重连蓝牙找不到设备 | `bt_test` 没在跑(PSCAN/ISCAN 关了) | 重新跑 `start-stop-daemon` 命令;**不要**手动跑 `bt_init.sh start`/`hciconfig up` 干预(bt_test 启动时会 kill 已有 hciattach 并自己重新拉) |
| `LMP Subversion 0x6f72` + `IC: NONE` | 模块状态污染判据 | 同上,只能断电;`0x8723`(ROM)是正常值 |

##### 模块 BT 状态指纹表(排查必备)

| 状态 | HCI Version | LMP Subversion | 波特率 | 结论 |
|---|---|---|---|---|
| ROM 干净态 | 0x08 | **0x8723** | 115200 应答 | 正常,hciattach 能完整下载固件 |
| 固件态(正常) | 0x07 | 0x6f72 | 1.5M | 正常!0x6f72 是 RTL8723D 固件态的值 |
| **被污染态** | 0x07 | 0x6f72 | 只在 1.5M 应答 SYNC 但 `IC: NONE` | hciattach 期望 ROM 0x8723,校验失败,**软恢复全部无效,只能断电** |

### 扬声器(LM4871 功放)

原理图:codec → HPOUTR(Pin 98)→ LM4871 功放 → 喇叭;LM4871 的 PA_SHDN = PB2,**硬件 10kΩ 下拉,上电即工作**——软件不需要 pa-pin/enable GPIO。codec 驱动、asound.conf 路由 SDK 都配好,**零代码改动**。

#### 配置核对(全部已有,不用改)

板级 DTS codec 节点(`board.dts`,SDK 已有):

```shell
&codec {
    /* avcc-external; */
    tx-hub-en;
    rx-sync-en;

    dac-vol     = <63>;     /* 默认值 63,范围 0-63 */
    dacl-vol    = <160>;    /* 左声道 DAC 音量,范围 0-255 */
    dacr-vol    = <160>;    /* 右声道 DAC 音量 */
    adc1-vol    = <160>;    /* ADC1 录音音量 */
    adc2-vol    = <160>;    /* ADC2 录音音量 */
    adc3-vol    = <160>;    /* ADC3 录音音量(接麦克风) */
    lineout-vol = <31>;     /* 线路输出音量,范围 0-31 */
    adc1-gain   = <31>;     /* ADC1 增益 */
    adc2-gain   = <31>;     /* ADC2 增益 */
    adc3-gain   = <31>;     /* ADC3 增益(接麦克风) */
    hpout-gain  = <7>;      /* 耳机输出增益,范围 0-7 */
    fminl-gain  = <0>;
    fminr-gain  = <0>;
    lineinl-gain = <0>;
    lineinr-gain = <0>;

    status = "okay";
};
```

`/etc/asound.conf`(rootfs 内,SDK 已有):`pcm.!default → Playback → softvol → dmix(hw 48000 stereo)` 路由;`rc.local` 开机 `amixer cset name="HPOUT Switch" 1`。

#### 内核 Menuconfig 配置

`CONFIG_SUNXI_CODEC=y` 模板已有(`config-5.4`),不用改。

#### 测试

```shell
# 1. 确认声卡
adb shell 'cat /proc/asound/cards'
# 预期: 0 [audiocodec    ]: audiocodec - audiocodec

# 2. 推一个 440Hz 正弦波(主机生成后推上去,3 秒)
adb push /tmp/sine.wav /tmp/sine.wav
adb shell 'aplay -D hw:audiocodec,0 /tmp/sine.wav'
# 预期: Playing WAVE ... 44100 Hz, Mono,喇叭出"嘟——"声

# 3. 蓝牙 A2DP 出声(蓝牙章测完之后):手机连上放歌,喇叭出声
```

<!-- 截图: 喇叭播放测试 -->

##### 音量控件默认值表(重要:sunxi codec 反向标定,别按直觉改)

**播放侧的音量控件是反向标定的(inverted TLV),amixer 值越大 = 寄存器值越小 = 音量越大。**
按"设最大值=最大音量"的直觉去改会把音量打到 -74dB(实测踩过的坑)。

| 控件 | 正常默认值 | 说明 |
|---|---|---|
| `DAC Digital Volume` | **63** | **inverted**:63=0dB,**设 0 = -74dB**(踩过的坑) |
| `DACL/DACR Volume` | **160** | 正向,0.75dB |
| `HPOUT Gain` | 7 | inverted,0dB |
| `HPOUT Switch` | on(rc.local 设) | — |
| `Soft Volume Master` | 255 | asound softvol,保持 255 |

> 录音控件(ADC*)是**正向**标定,和播放侧相反,别混淆。
> amixer 只显示控件值,不显示寄存器实际值;验证寄存器:
> ```shell
> adb shell 'head -2 /sys/kernel/debug/regmap/$(ls /sys/kernel/debug/regmap/ | grep -i codec | head -1)/registers'
> # 预期: 000: 80000000(DVOL=0=0dB)   004: 0001a0a0(DACL/R=160=0.75dB)
> ```

##### 扬声器排错速查

| 现象 | 原因 | 处理 |
|---|---|---|
| 完全无声 | `HPOUT Switch` off | `amixer -Dhw:audiocodec cset numid=40 on` |
| 声音极小/失真 | `DAC Digital Volume` 被设成 0(=-74dB) | **设回 63**:`amixer -Dhw:audiocodec cset numid=16 63` |
| `Device or resource busy` | A2DP/bluealsa 占着声卡 | 先 `kill $(pidof bt_test)`,测完重新拉起 |

### 麦克风(GMI4015P 差分模拟麦,ADC3 通道)

原理图:差分模拟麦——正极经 C85 100nF → MICIN3P(Pin87),负极经 C86 100nF → MICIN3N(Pin88);偏置网络全在硬件上(AVCC→R148 75Ω→R147 1kΩ 偏正极,R146 1kΩ 偏负极)。走 codec 的 **ADC3 通道**。**零代码改动**(DTS/驱动/rc.local/asound.conf 全部默认)。

#### 配置核对(全部已有,不用改)

- DTS:`adc3-vol = <160>`、`adc3-gain = <31>`(上面的 codec 节点里,注释即"接麦克风")
- 驱动控件(`snd_sun8iw20_codec.c`):`MIC3 Input Select`、`MIC3 Switch`、`ADC3 Gain`(0-31,正向标定,step 1dB)
- `rc.local` 开机路由(上面的 rc.local 内容里):

```shell
amixer -Dhw:audiocodec cset name='MIC3 Input Select' 1
amixer -Dhw:audiocodec cset name='MIC3 Switch' 1
amixer -Dhw:audiocodec cset name='ADC3 Gain' 19
```

- `/etc/asound.conf` 的 `CaptureMic`:hooks 设 Input1/2 Mux + LINEINL/R Switch + MIC3 选择 → dsnoop 16k 3ch → `ttable.0.2 1` 取第 3 路(ADC3)

> `MIC3 Input Select` 的语义:值为 1 时 ADC3 采集 MIC3P/N 差分对。
> `ADC3 Gain 19`(≈19dB)是 rc.local 的实测工作点;DTS 默认 31 偏大,近讲说话建议 15~20,太大会削顶。

#### 测试

```shell
# 录 5 秒,16kHz 单声道 S16(经 asound CaptureMic 路由)
adb shell 'arecord -d 5 -r 16000 -f S16_LE /tmp/test.wav'

# 回放(经 HPOUT→LM4871→喇叭)
adb shell 'aplay /tmp/test.wav'
# 预期: 能正常听到自己的声音
```

<!-- 截图: arecord/aplay -->

##### 麦克风排错速查

| 现象 | 原因 | 处理 |
|---|---|---|
| 录出来全是静音 | `MIC3 Switch` 没开 / `MIC3 Input Select` 不为 1 | rc.local 开机已设;手动:`amixer -Dhw:audiocodec cset name='MIC3 Switch' 1` |
| 声音太小 | `ADC3 Gain` 低 | `amixer -Dhw:audiocodec cset name='ADC3 Gain' 19` |
| 声音破/削顶 | `ADC3 Gain` 太大 | 降到 15 左右 |
| 回放无声 | 播放侧问题 | 见扬声器章排错速查 |

## 通用排错心法(全部实测踩出来的)

1. **模块常供电的坑**:改 WiFi/BT 固件或 BT 状态异常后,断电要拔**所有**线(含 adb USB),30 秒以上再上电(去耦电容放电);RTL8723DS 无复位脚,软恢复全部无效;
2. **开机顺序**:BT 先、WiFi 后(rc.local 已固化);8723ds 不自动加载是故意的;
3. **换驱动后不能只换 .ko**:面板/触摸是内建(=y)的,改一次烧一次整镜像;
4. **音频组件音量标定方向不统一**:播放 DAC 反向、录音 ADC 正向,改前先读 `/sys/kernel/debug/regmap` 里的寄存器实际值;
5. **build 失败先看日志**:全志 mkcommon 输出到 `script` 日志,host 工具并行问题一律 `-j1`;源码包下载失败可手动下到 `openwrt/dl/`(校验 PKG_HASH);
6. **git 各组件独立仓库**(kernel/device/openwrt-target/feeds/platform 子仓库),commit 在各自仓库做。

## 附:各阶段产物镜像(参考)

| 阶段 | 镜像 md5(均 `out/t113_linux_zgl_board_uart0.img`) |
|---|---|
| 串口 | c541e473...(2026-10-15) |
| 屏幕 | a10c80b9e39cbbba3587c1af27450059 |
| 触摸 | 09720c63035d698fe6458333f1aab491 |
| WiFi | 1e90397e953a80b446778124ed46b977(21022720 字节) |
| 蓝牙 | 24b93df616c7c36ca6ad4cd694b90174(21153792 字节) |
| **从零复刻(按本教程,new_build_t113,2026-10-20)** | **388f7f14122ec69602e785f83b94ecb8**(21153792 字节,板上 7 外设实测全通过;见 `docs/peripheral/08-replication.md`) |
