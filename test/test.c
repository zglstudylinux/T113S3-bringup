/*
 * test.c - zgl_board BSP 三阶段调试测试文档与诊断工具
 *
 * BSP:    t113-zgl_board (Allwinner T113 / Tina5.0 V1.2 SDK)
 * 硬件:    D240C2501V1 (2.4" 480x640 MIPI DSI, ST7701SN) + dx_touch (I2C 0x38)
 * 镜像位置: out/t113_linux_zgl_board_uart0.img  (~21 MB, PhoenixSuit 烧录)
 *
 * 三个镜像快照 (已备份在 /tmp/):
 *   /tmp/t113_stage_A.img  SHA-256 fad5d9659bd2f5eef44eee9a391dc7719e99c89a2e3b866b341cf2c4027e1b25  (UART only)
 *   /tmp/t113_stage_B.img  SHA-256 3540b3b3c8fd69dbae12ccc1341261e49b11d40101d05c04b345d77c92d1de62  (UART + MIPI panel)
 *   /tmp/t113_stage_C.img  SHA-256 ca1dfd3c1b7f1cdb1a75ab43c257976f651ec7f379f487030a390af8e0be92f6  (UART + MIPI + Touch)
 *
 * 建议测试顺序: A → B → C 逐个烧录验证,便于逐阶段排错。
 */

/* ============================================================================
 * 阶段 A: 调通 UART
 * ============================================================================
 *
 * 解决的问题:
 *   - BSP 原 [uart_para] 把 UART3 配在 PG8/PG9 mux5,但原理图上调试串口座
 *     U24 (MX1.25-3P) 实际接 PB6/PB7 mux7。PG8/PG9 物理接到 WiFi 模块
 *     UART1_RTS/CTS,所以 BSP 的 console 输出被送到了 WiFi,而不是 USB-TTL。
 *   - env.cfg 的 earlycon 地址 0x05000000 是错的(T113 UART3 实际是
 *     0x02500c00),并且 setargs_nand 引用了未定义的 ${earlyprintk}。
 *
 * 修改文件:
 *
 *   1) device/config/chips/t113/configs/zgl_board/sys_config.fex  (~2 行)
 *      [uart_para]
 *      uart_debug_port = 3
 *      uart_debug_tx   = port:PB06<7><1><default><default>   ← 原 PG08<5>
 *      uart_debug_rx   = port:PB07<7><1><default><default>   ← 原 PG09<5>
 *
 *   2) device/config/chips/t113/configs/zgl_board/openwrt/env.cfg  (4 行)
 *      earlycon=uart8250,mmio32,0x02500c00                    ← 原 0x05000000
 *      setargs_nand=... earlyprintk=${earlycon} ...            ← 原 ${earlyprintk}
 *      setargs_nand_ubi=... earlyprintk=${earlycon} ...       ← 原 ${earlyprintk}
 *      setargs_mmc=... earlyprintk=${earlycon} ...            ← 原 ${earlyprintk}
 *
 *   3) device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts  (~20 行)
 *      uart3_pins_a: pins = "PB6", "PB7";   function = "uart3";   ← 原 PG8/PG9
 *      uart3_pins_b: pins = "PB6", "PB7";   function = "gpio_in";
 *      &twi3 { ... status = "disabled"; };                   ← 整段删 ac108@3B
 *                                                              (AC108 codec 不在板上,
 *                                                               TWI3 占 PB6/PB7 让给 UART3)
 *
 * 测试步骤:
 *   硬件接线 (3.3V TTL,严禁接 RS-232):
 *     USB-TTL TX  → 板子 U24 pin 1 (UART3_RX = T113 PB7)
 *     USB-TTL RX  → 板子 U24 pin 2 (UART3_TX = T113 PB6)
 *     USB-TTL GND → 板子 U24 pin 3
 *
 *   主机端开 115200 8N1 (关硬件/软件流控):
 *     python3 -m serial.tools.miniterm /dev/ttyUSB0 115200 --parity N --stopbits 1 --bytesize 8
 *     或 minicom -D /dev/ttyUSB0 -b 115200
 *
 *   冷启动后预期日志顺序:
 *     1. "HELLO! BOOT0 is starting!"                  ← boot0
 *     2. "HELLO! SBOOT is starting!"                  ← SBOOT
 *     3. "U-Boot 2018.07-..."                         ← U-Boot
 *     4. "Linux version 5.4.61..."                    ← kernel (earlycon 修好后可见)
 *     5. "Please press Enter to activate this console" ← OpenWrt askfirst
 *
 *   进 OpenWrt shell 后跑:
 *     cat /proc/cmdline       # 应含 console=ttyS3,115200
 *     dmesg | grep -E 'ttyS|sunxi-uart'    # 应看到 uart3 注册
 *     ls /dev/ttyS*                       # 应有 ttyS0~ttyS5
 *
 * 反向验证:
 *   之前如果把 USB-TTL 接到 PG8/PG9 看到过任何输出,现在应该完全没了。
 *   PG8/PG9 物理上已让给 WiFi 模块 (UART1_RTS/CTS),不会再有 UART 输出。
 *
 * 通过判据: 看到 boot0→SBOOT→U-Boot→kernel→OpenWrt 全链路日志
 */

/* ============================================================================
 * 阶段 B: 调通 MIPI 屏 (D240C2501V1 + ST7701SN)
 * ============================================================================
 *
 * 解决的问题:
 *   - BSP 默认 &lcd0 配的是 LVDS 1024x600 (default_lcd),不是原理图 FPC2 上的
 *     D240C2501V1 DSI 屏。
 *   - SDK 没有 ST7701SN 驱动,需要新增一个 disp2 panel 驱动。
 *
 * 修改/新增文件:
 *
 *   1) bsp/drivers/video/sunxi/disp2/disp/lcd/st7701sn.h  (新增,~10 行)
 *      extern struct __lcd_panel st7701sn_panel;
 *
 *   2) bsp/drivers/video/sunxi/disp2/disp/lcd/st7701sn.c  (新增,~230 行)
 *      - LCD_open_flow/close_flow: power_on → panel_init → tcon_enable → bl_open
 *      - LCD_panel_init: 发送 ~42 条 ST7701SN DCS init 命令(从 eval 报告
 *        docs/touch_screen/t113-mipi-touchscreen-evaluation.md §6.2 提取)
 *      - struct __lcd_panel: lcd_driver_name="st7701sn", lcd_if=4 (DSI),
 *        lcd_dsi_if=0 (video mode), 480x640, 21MHz pclk, 2-lane
 *
 *   3) bsp/drivers/video/sunxi/disp2/disp/lcd/Kconfig  (新增 ~10 行)
 *      config LCD_SUPPORT_ST7701SN
 *          bool "LCD support st7701sn panel (D240C2501V1)"
 *          default y
 *
 *   4) bsp/drivers/video/sunxi/disp2/disp/Makefile  (新增 1 行)
 *      disp-$(CONFIG_LCD_SUPPORT_ST7701SN) += lcd/st7701sn.o
 *
 *   5) bsp/drivers/video/sunxi/disp2/disp/lcd/panels.c  (新增 4 行)
 *      #if IS_ENABLED(CONFIG_LCD_SUPPORT_ST7701SN)
 *          &st7701sn_panel,
 *      #endif
 *
 *   6) device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts  (修改 ~30 行)
 *      &lcd0 {
 *          lcd_used            = <1>;
 *          lcd_driver_name     = "st7701sn";        ← 原 "default_lcd"
 *          lcd_if              = <4>;               ← 原 <3> (LVDS)
 *          lcd_dsi_if          = <0>;
 *          lcd_dsi_lane        = <2>;
 *          lcd_dsi_format      = <0>;               ← RGB888
 *          lcd_x               = <480>;             ← 原 <1024>
 *          lcd_y               = <640>;              ← 原 <600>
 *          lcd_dclk_freq       = <21>;              ← 原 <52>
 *          lcd_bl_pwm_value    = <200>;
 *          lcd_pwm_ch          = <7>;
 *          lcd_gpio_0          = "PD20";            ← LCD_RST active low
 *          lcd_bl_en           = "PD22";            ← backlight PWM7
 *          lcd_hbp = <10>; lcd_ht = <502>; lcd_hspw = <2>;
 *          lcd_vbp = <30>; lcd_vt = <702>; lcd_vspw = <2>;
 *          pinctrl-0 = <&dsi2lane_pins_a>;        ← 原 <&lvds0_pins_a>
 *          pinctrl-1 = <&dsi2lane_pins_b>;
 *      };
 *
 * 测试步骤:
 *   烧录 Stage B 镜像到板子,USB-TTL 接好,串口进 OpenWrt shell:
 *
 *     dmesg | grep -iE 'st7701sn|panel|lcd0|dsi|disp'
 *     # 预期输出类似:
 *     # sunxi lcd0 panel: st7701sn
 *     # DE2.0 init ok
 *     # DSI 2-lane
 *
 *     ls /dev/fb* /dev/disp
 *     # 预期: /dev/fb0 存在
 *
 *     echo 1 > /sys/class/disp/disp/attr/enable
 *     cat /dev/urandom > /dev/fb0 &    # 雪花屏,验证 framebuffer 工作
 *
 *     # 预期看到:
 *     # - 屏背光亮 (PD22/PWM7 输出 200/255)
 *     # - 屏上有图像 (颜色可能不对,但有图 = driver 加载成功)
 *
 * 通过判据:
 *   - 屏背光亮
 *   - 屏上有图像(任何颜色都行,纯黑=DSI 没通讯成功)
 *   - /dev/fb0 可读写
 *
 * 失败排查:
 *   屏不亮     → grep st7701sn out/.../boot.img  (解压看 panel 驱动是否编入)
 *   亮但全黑   → 示波器量 PD0-PD5 DSI 信号;查 42 条 DCS 命令是否真的发送
 *   颜色乱     → 改 lcd_dsi_format 0↔1↔2
 *   只显示一半 → 调 hfp/hspw/hbp/vfp/vspw/vbp
 *   方向反     → init 序列最后加 0x36 (MADCTL) 命令
 */

/* ============================================================================
 * 阶段 C: 调通触摸 (dx_touch @ I2C 0x38)
 * ============================================================================
 *
 * 解决的问题:
 *   - SDK 默认 GT9xxnew (Goodix) 模板的 ctp@14 disabled,且 PB7 与 wifi 唤醒
 *     脚冲突。
 *   - 实际硬件触摸 IC 是 dx_touch (大显自研),I2C 地址 0x38,接在 TWI1
 *     (PE0=SCL, PE1=SDA),RST=PE4, INT=PE5。
 *
 * 修改/新增文件:
 *
 *   1) kernel/linux-5.4/drivers/input/touchscreen/dx_touch/dx_touch.c
 *      (新增,从 docs/touch_screen/touch_screen_code/dx_touch/dx_touch.c 复制,
 *       修改了一处:.remove 函数从 void 改为 int (Linux 5.4 兼容))
 *
 *   2) kernel/linux-5.4/drivers/input/touchscreen/dx_touch/Makefile  (新增 1 行)
 *      obj-y += dx_touch.o
 *
 *   3) kernel/linux-5.4/drivers/input/touchscreen/dx_touch/Kconfig  (新增 9 行)
 *      config TOUCHSCREEN_DX_TOUCH
 *          tristate "DX Touchscreen (D240C2501V1 panel)"
 *          depends on I2C && INPUT
 *
 *   4) kernel/linux-5.4/drivers/input/touchscreen/Kconfig  (新增 1 行)
 *      source "drivers/input/touchscreen/dx_touch/Kconfig"
 *
 *   5) kernel/linux-5.4/drivers/input/touchscreen/Makefile  (新增 1 行)
 *      obj-$(CONFIG_TOUCHSCREEN_DX_TOUCH) += dx_touch/
 *
 *   6) device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts
 *      a) twi1_pins_a/b: pins = "PE0", "PE1";  ← 原 "PE14", "PE15"
 *      b) &twi1: status = "okay"; ← 原 "disabled"
 *         + 新增 dxts@38 子节点 (compatible="dx,touch", reg=<0x38>,
 *           interrupts=<5 5 IRQ_TYPE_EDGE_FALLING>, reset-gpios=<&pio 5 4>)
 *
 *   7) device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4  (新增 1 行)
 *      CONFIG_TOUCHSCREEN_DX_TOUCH=y
 *
 * 测试步骤:
 *   烧录 Stage C 镜像,串口进 OpenWrt shell:
 *
 *     dmesg | grep -iE 'dx_touch|dx,touch|twi1|input'
 *     # 预期:
 *     # dx_touch 0-0038: probed
 *     # input: dx_touch as /devices/.../input/input0
 *
 *     opkg update
 *     opkg install evtest i2c-tools
 *
 *     i2cdetect -y 1        # TWI1 是 bus 1
 *     # 预期: 0x38 行显示 "38" 或 "UU"
 *
 *     ls /dev/input/
 *     # 预期: event0 (dx_touch)
 *
 *     evtest /dev/input/event0
 *     # 用手指触摸屏幕,预期看到:
 *     # ABS_MT_POSITION_X  值范围 0..479
 *     # ABS_MT_POSITION_Y  值范围 0..639
 *
 * 通过判据:
 *   - i2cdetect -y 1 看到 0x38
 *   - evtest 触摸产生 ABS_MT_POSITION_X/Y 事件
 *   - 坐标范围正确
 *
 * 失败排查:
 *   i2cdetect 看不到 0x38      → 示波器量 PE0/PE1 波形;
 *                                  试 0x14 (GT911 备用地址);
 *                                  检查 PE4 复位时序
 *   i2cdetect 看到但 evtest 无事件 → 量 PE5 是否有下降沿;
 *                                   试 IRQ_TYPE_LEVEL_LOW 替换 FALLING
 *   触摸有事件但 X 反            → 注释 dx_touch.c 中 input_x = 480 - ... 那行
 *   触摸有事件但 Y 反            → 翻转 Y 计算
 *   触摸点和显示对不上          → 在 ST7701SN init 序列加 0x36 (MADCTL)
 */

/* ============================================================================
 * 编译产物路径与 SHA-256 速查表
 * ============================================================================
 *   /tmp/t113_stage_A.img   fad5d9659bd2f5eef44eee9a391dc7719e99c89a2e3b866b341cf2c4027e1b25
 *   /tmp/t113_stage_B.img   3540b3b3c8fd69dbae12ccc1341261e49b11d40101d05c04b345d77c92d1de62
 *   /tmp/t113_stage_C.img   ca1dfd3c1b7f1cdb1a75ab43c257976f651ec7f379f487030a390af8e0be92f6
 * ============================================================================
 */

/* main() 留空 (本文件是文档性质,实际逻辑已在镜像里) */
int main(void) { return 0; }
