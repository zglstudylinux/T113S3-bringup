# 03 — 触摸(dx_touch,I2C 0x38,FT5x06 类协议)

> 状态: **✅ 已调通**(2026-10-16,镜像 md5 09720c63035d698fe6458333f1aab491)  
> 平台: T113-S3 / Tina 5.0 V1.2 / Linux 5.4.61 / `t113-zgl_board`  
> 现象: `i2cdetect -y 1` 见 0x38(UU),`event4` 上报坐标,evtest/hexdump 验证正常

---

## 1. 原理图与硬件

触摸 IC 焊在屏 FPC 上(屏厂配套),通过 FPC1 与 SoC 相连:

| 信号 | T113 引脚 | 网络 | 说明 |
|---|---|---|---|
| I2C SCL | **PE0** | TP_SCL | TWI1,RN1 10k 上拉 3V3 |
| I2C SDA | **PE1** | TP_SDA | TWI1,RN1 10k 上拉 3V3 |
| 中断 INT | **PE5** | TP_INT | RN1 10k 上拉,**下降沿**有效 |
| 复位 RST | **PE4** | TP_RST | 拉低复位(RN1 上拉默认高) |

- 触摸 IC: compatible `dx,touch`(深圳大显,FT5x06 类寄存器协议),I2C 地址 **0x38**
- 分辨率 480×640,竖屏;**X 轴在驱动内做 480-x 镜像**(屏的物理方向与 IC 默认方向相反)

## 2. 修改文件与 diff(逐文件)

### 2.1 新增 `kernel/linux-5.4/drivers/input/touchscreen/dx_touch/dx_touch.c`

屏厂驱动**修复版**(约 350 行)。**关键修复**(相对屏厂原版 `docs/touch_screen/touch_screen_code/dx_touch/dx_touch.c`):

```diff
     gpio_direction_output(ts->rst_pin,1);
     msleep(2);
     gpio_direction_output(ts->irq_pin,0);
     msleep(2);
     gpio_direction_output(ts->rst_pin,1);
     msleep(16);
-    gpio_direction_output(ts->irq_pin, 1);
-  //  gpio_direction_output(ts->irq_pin, 0);
+    gpio_direction_input(ts->irq_pin);   /* 关键：把 IRQ pin 切回输入模式，否则 devm_request_threaded_irq 会失败 */
     msleep(50);
```

> **为什么必须改**:复位时序里 INT 脚被临时拉低再拉高(FALSE 触发序列),原版结束时 INT 停留在**输出高**状态。之后 `gpio_to_irq()` + `devm_request_threaded_irq()` 申请中断时,pinctrl 发现该脚被占用为输出,中断申请**必定失败**,probe 返回负值,触摸完全无响应。修复版在申请中断前把 INT 切回输入模式。

驱动其余要点(读 `dx_touch.c` 可见):

```c
#define lcdx    480
#define lcdy    640
...
// 读点坐标时 X 轴镜像:
//  input_x  = ((touch_data[0]&0x0f)<<8) | touch_data[1];   ← 屏厂原版(方向错)
     input_x  = 480-(((touch_data[0]&0x0f)<<8) | touch_data[1]);   ← 修复版
//    FT_SWAP(input_x,input_y);   //横屏时打开，竖屏时要屏蔽
```

设备树属性名(驱动读取,**属性名与 dts 必须一致**):

```c
ts->rst_pin = of_get_named_gpio(np, "reset-gpio", 0);   /* 注意是 reset-gpio, 不是 reset-gpios */
ts->irq_pin = of_get_named_gpio(np, "touch-gpio", 0);   /* 注意是 touch-gpio, 不是 interrupts */
of_property_read_u32(np, "max-x", &ts->abs_x_max);
of_property_read_u32(np, "max-y", &ts->abs_y_max);
```

### 2.2 新增 `kernel/linux-5.4/drivers/input/touchscreen/dx_touch/Makefile`

```makefile
obj-y += dx_touch.o
```

### 2.3 修改 `kernel/linux-5.4/drivers/input/touchscreen/Kconfig`

在 `config TOUCHSCREEN_IQS5XX` 块之后、`source "drivers/input/touchscreen/gslx680new/Kconfig"` 之前插入:

```diff
@@ -1324,6 +1324,14 @@ config TOUCHSCREEN_IQS5XX
 	  To compile this driver as a module, choose M here: the
 	  module will be called iqs5xx.
 
+config TOUCHSCREEN_DX_TOUCH
+	tristate "DX touch screen (D240N2501V1 panel)"
+	depends on I2C
+	help
+	  Say Y to enable support for the DX touch IC bundled with
+	  the D240N2501V1 2.4" 480x640 panel (ST7701SN).
+	  I2C address 0x38, INT active-low, RST active-high.
+
 source "drivers/input/touchscreen/gslx680new/Kconfig"
```

### 2.4 修改 `kernel/linux-5.4/drivers/input/touchscreen/Makefile`

```diff
@@ -114,3 +114,4 @@ obj-$(CONFIG_TOUCHSCREEN_IQS5XX)	+= iqs5xx.o
 obj-$(CONFIG_TOUCHSCREEN_GSLX680NEW)    += gslx680new/
 obj-$(CONFIG_TOUCHSCREEN_FTS)    += focaltech_touch/
 obj-$(CONFIG_INPUT_TOUCHSCREEN)    += gt9xxnew/
+obj-$(CONFIG_TOUCHSCREEN_DX_TOUCH) += dx_touch/
```

### 2.5 修改 `device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts`

a) pinctrl 组 `twi1_pins_a/b` 引脚必须是 PE0/PE1(对照原理图;evb1 模板默认是 PE14/PE15):

```diff
 	twi1_pins_a: twi1@0 {
-		pins = "PE14", "PE15";
+		pins = "PE0", "PE1";          /* Touch I2C on zgl_board */
 		function = "twi1";
 		drive-strength = <10>;
 	};
 
 	twi1_pins_b: twi1@1 {
-		pins = "PE14", "PE15";
+		pins = "PE0", "PE1";
 		function = "gpio_in";
 	};
```

b) `&twi1` 控制器启用 + 挂触摸节点(完整节点,与驱动属性名一一对应):

```diff
 &twi1 {
 	clock-frequency = <400000>;
 	pinctrl-0 = <&twi1_pins_a>;
 	pinctrl-1 = <&twi1_pins_b>;
 	pinctrl-names = "default", "sleep";
-	status = "disabled";
+	status = "okay";
+
+	dxts@38 {
+		compatible    = "dx,touch";
+		reg           = <0x38>;
+		max-x         = <480>;
+		max-y         = <640>;
+		touch-gpio    = <&pio PE 5 GPIO_ACTIVE_HIGH>;
+		reset-gpio    = <&pio PE 4 GPIO_ACTIVE_HIGH>;
+		status        = "okay";
+	};
 };
```

> **属性名陷阱**:不要写成主流触摸驱动的 `interrupts = <...>` + `reset-gpios = <...>` 写法 —— `dx,touch` 驱动只认 `touch-gpio` / `reset-gpio`(见 §2.1)。两套属性混用会导致 probe 时 `of_get_named_gpio` 失败。

### 2.6 修改 `device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4`

```diff
 CONFIG_TOUCHSCREEN_GT9XXNEW_TS=y     ← 可保留, 与 DX_TOUCH 不冲突(不同 I2C 地址, 未挂节点不 probe)
+CONFIG_TOUCHSCREEN_DX_TOUCH=y
```

### 2.7 修改 `openwrt/target/t113/t113-zgl_board/defconfig`(测试工具)

```diff
-# CONFIG_PACKAGE_evtest is not set
+CONFIG_PACKAGE_evtest=y
...
-# CONFIG_PACKAGE_i2c-tools is not set
+CONFIG_PACKAGE_i2c-tools=y
```

## 3. 编译打包

```bash
cd /home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2
. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board
./build.sh kernel -j8        # dx_touch 内建 + dts
./build.sh openwrt_rootfs -j1 # evtest + i2c-tools 进 rootfs(改 defconfig 必须重编)
./build.sh pack
# 产物: out/t113_linux_zgl_board_uart0.img (md5 09720c63…)
```

> **evtest 源码包下载失败的处理**(国内网络常见):`make[3] -C .../feeds/utils/evtest compile` 报
> `curl: (28) Failed to connect ... Download failed` 时,手动下载放入 dl 缓存:
> ```bash
> curl -fL -o openwrt/dl/evtest-1.34.tar.xz \
>   http://deb.debian.org/debian/pool/main/e/evtest/evtest_1.34.orig.tar.xz
> sha256sum openwrt/dl/evtest-1.34.tar.xz
> # 必须等于 Makefile 里 PKG_HASH: e49f1f160b30c8f7c2a4caef5ab655f1caf816483d19fdedd6db2d251d7ab80e
> ```
> 然后重新 `./build.sh openwrt_rootfs -j1`。

## 4. 测试与结果(2026-10-16 adb 实测)

### 4.1 I2C 总线

```text
$ adb shell ls /dev/i2c-*
/dev/i2c-1

$ adb shell cat /sys/bus/i2c/devices/*/name
touch                    ← 1-0038 (dx_touch)
SUNXI TWI(0x02502400)    ← twi1 控制器

$ adb shell i2cdetect -y 1
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
30:                         -- -- -- UU -- -- -- -- 
                             ^^ 0x38 = UU(已被驱动占用, 正常)
```

### 4.2 输入设备

```text
$ adb shell cat /proc/bus/input/devices | grep -A8 "dx touch"
I: Bus=0018 Vendor=0000 Product=0000 Version=0000
N: Name="dx touch screen"
P: Phys=
S: Sysfs=/devices/platform/soc@3000000/2502400.twi/i2c-1/1-0038/input/input4
H: Handlers=event4 
B: EV=b
B: KEY=400 0 0 0 0 0 0 0 0 0 0        ← BTN_TOUCH(330)
B: ABS=2658000 3                      ← ABS_X/ABS_Y/ABS_MT_SLOT
```

### 4.3 evtest 能力与事件

```text
$ adb shell evtest /dev/input/event4
Input device name: "dx touch screen"
Supported events:
  Event type 1 (EV_KEY)
    Event code 330 (BTN_TOUCH)
  Event type 3 (EV_ABS)
    Event code 0 (ABS_X)   Min 0   Max 480
    Event code 1 (ABS_Y)   Min 0   Max 640
    Event code 47 (ABS_MT_SLOT)
```

划屏原始事件(hexdump,一个触摸点 X≈276=0x114, Y≈114=0x72):

```text
$ adb shell hexdump /dev/input/event4
0003 0039 0001 0000   ← ABS_MT_SLOT = 0
0003 0035 0114 0000   ← ABS_MT_POSITION_X = 0x114 (276)
0003 0036 0072 0000   ← ABS_MT_POSITION_Y = 0x72 (114)
0001 014a 0001 0000   ← BTN_TOUCH = 1 (按下)
0003 0000 0114 0000   ← ABS_X = 276
0003 0001 0072 0000   ← ABS_Y = 114
0000 0000 0000 0000   ← SYN_REPORT
```

### 4.4 完成判据(全部通过)

- [x] `i2cdetect -y 1` 见 0x38(UU)
- [x] `/proc/bus/input/devices` 有 `dx touch screen`(event4)
- [x] evtest 显示 BTN_TOUCH + ABS_X(0-480)/ABS_Y(0-640)
- [x] 划屏连续报点,坐标范围正确;**点击位置与屏幕显示位置一致**(X 已镜像)

### 4.5 方向校准(如遇不一致)

| 现象 | 修改(`dx_touch.c` 读点函数) |
|---|---|
| 左右反 | 恢复 `input_x = ((touch_data[0]&0x0f)<<8) | touch_data[1];`(去掉 480-) |
| 上下反 | `input_y = 640 - (((touch_data[1]&0x0f)<<8) | touch_data[2]);` |
| 横竖颠倒 | 打开 `FT_SWAP(input_x,input_y);` |

## 5. 涉及文件汇总

| 仓库 | 文件 | 性质 |
|---|---|---|
| kernel/linux-5.4 | `drivers/input/touchscreen/dx_touch/dx_touch.c` | 新增(修复版) |
| kernel/linux-5.4 | `drivers/input/touchscreen/dx_touch/Makefile` | 新增 |
| kernel/linux-5.4 | `drivers/input/touchscreen/Kconfig` | 修改(+8 行) |
| kernel/linux-5.4 | `drivers/input/touchscreen/Makefile` | 修改(+1 行) |
| device/config/chips/t113 | `configs/zgl_board/linux-5.4/board.dts` | 修改(twi1_pins PE0/PE1 + dxts@38) |
| device/config/chips/t113 | `configs/zgl_board/linux-5.4/config-5.4` | 修改(+1 行) |
| openwrt/target/t113 | `t113-zgl_board/defconfig` | 修改(evtest/i2c-tools=y) |

Patch 导出:
