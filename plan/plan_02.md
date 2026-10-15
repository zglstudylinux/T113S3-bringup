# Plan_02: zgl_board 外设 Bring-up — UART → MIPI 屏 → 触摸 → WiFi → BT → 扬声器

> 创建时间: 2026-10-15  
> 目标 BSP: `t113-zgl_board` (Allwinner T113-S3 / Tina5.0 V1.2, Linux 5.4.61)  
> **plan_01 已作废**(基于旧原理图 `schematic-analysis-1785750417694.md`;新原理图 `sch/T113.pdf`/`T113.md` 修正了 WiFi/BT 方案: RTL8723DS 而非 XR,功放 PA_SHDN 在 PB2 等)  
> Git: https://github.com/zglstudylinux/T113S3-bringup.git (docs 目录)

---

## 权威输入

| 资料 | 位置 |
|---|---|
| 原理图(权威) | `docs/sch/T113.pdf` + `docs/sch/T113.md`(7页引脚分析) |
| 移植参考包(已验证) | `/home/zgl/Linux_SDK/T113/zgl_board_peripheral_porting_kit/` |
| 官方文档 | `docs/datasheet/`(数据手册/用户手册/pinout + Tina Wi-Fi 开发指南/模组移植指南 + 蓝牙开发指南/模组移植指南) |
| 教程模板 | `docs/Tutorial_Doc/T113_SDK.md` |
| 屏评估报告 | `docs/touch_screen/t113-mipi-touchscreen-evaluation.md` |

## 硬件事实(基于新原理图 T113.md)

| 功能 | 引脚 | 说明 |
|---|---|---|
| 调试串口 UART3 | **PB6(TX)/PB7(RX)** | U24 3P 调试座 |
| MIPI DSI | PD0-PD5(2-lane+clk) | FPC1,2 数据 lane |
| LCD 复位 | PD20 | 低有效 |
| LCD 背光 | PD22(PWM7) | MT9201 boost EN,R120 10k 下拉默认关 |
| 触摸 I2C | PE0(SCL)/PE1(SDA) | twi1,RN1 10k 上拉 |
| 触摸复位/中断 | PE4 / PE5 | 低有效复位 |
| WiFi SDIO | PG0-PG5 | RTL8723DS 4bit,常供电**无 REG_ON** |
| BT HCI | PG6-PG9(UART1 四线) | RTL8723DS H5,**无 RESET 脚** |
| 功放 PA_SHDN | **PB2** | LM4871 SHUTDOWN(高有效);R138 100k 下拉=**默认使能** |
| 音频输出 | HPOUTR(pin98) | →C142→R136→LM4871 IN-→喇叭 CN2(单声道,HPOUTL 接地) |
| UART0(BLE Mesh) | PE2/PE3 | MX-02 模块(本次不做,协议资料缺) |

**屏**: D240N2501V1(2.4" 480x640 IPS,ST7701SN,DSI 2-lane video mode,44条 init)  
**触摸**: dx_touch(I2C 0x38,FT5x06 类协议)

## 现状核对(2026-10-15,逐项验证过)

| 外设 | 状态 | 缺口 |
|---|---|---|
| UART3 | ✅ board.dts/sys_config.fex 已配 PB6/PB7,twi3 已禁,openwrt/env.cfg earlycon=0x02500c00 已修 | 仅验证;`bsp/env.cfg` earlycon 仍 0x05000000 需同步 |
| MIPI 屏 | ❌ 内核树无面板驱动(board.dts 指向的 `st7701sn` 名不存在);`lcd_dclk_freq=21` < 28MHz 硬下限;uboot-board.dts 还是 LVDS 1024x600 | 全套移植(参考包有已验证版 `st7701sn_d240n2501v1`) |
| 触摸 | ⚠️ 驱动在但是旧版(复位序列缺 `gpio_direction_input(irq_pin)` → IRQ 注册必失败);dts 属性名错(用 `interrupts`/`reset-gpios`,驱动读 `touch-gpio`/`reset-gpio`) | 换参考包修复版 |
| WiFi | ❌ rfkill/wlan 全禁;wlan_regon=PB7 与 UART3 冲突;内核 RTL8723DS 未开(现 XR829_WLAN=m);openwrt 缺 kmod-net-rtl8723ds/rtl8723ds-firmware | 移植(rfkill/wlan 两个内核补丁必交) |
| BT | ❌ uart1 两线 disabled;BT_HCIUART_RTL3WIRE 未开;缺 rtk_hciattach(锁版 100096B)/btmanager 4.0.3(libbtmg 285460B)/开机时序 | 移植 |
| 音频 | ⚠️ codec okay;LM4871 硬件默认使能;asound.conf/rc.local amixer 已有 | 补测试 |
| adb | 目标板 adbd=y | 主机 adb 已装(~/.local/bin,v37.0.1) |

## 执行策略

参考包为蓝本(文件级拷贝+自动备份),**按模块分阶段**: 改代码 → `script -qec` pty 编译 → 新镜像烧录 → adb/串口验证 → `docs/peripheral/NN-xxx.md` → 组件仓库 commit(`t113: zgl_board xxx` 风格)→ patch 导出到 bringup → docs push。

### git 仓库布局
- **docs/**(本仓库, main → T113S3-bringup): 文档+补丁+截图;datasheet 大 PDF 不入库
- **device/config/chips/t113**: 每阶段 commit
- **kernel/linux-5.4**: 阶段2/3/4 commit + patch 导出
- **openwrt/target/t113**、**openwrt/package/**(feeds utils/btmanager): 阶段3/4/5 commit
- **platform/**: 阶段5(rtk_hciattach/btmanager 二进制锁版)

## 阶段

| # | 外设 | 核心改动 | 验证 |
|---|---|---|---|
| 0 | 准备 | adb 装好;git 代理修正(失效的 .153:7897 已注释,直连 OK);docs git init+push;plan_02 | — |
| 1 | 串口 | `bsp/env.cfg` earlycon=0x02500c00 同步 | 串口全链路+adb devices |
| 2 | MIPI 屏 | 内核 16 文件中显示相关(st7701sn_d240n2501v1.c 44条init/fl7707n_mipi/panels.c/.h/lcd Kconfig/disp Makefile + disp_display.c 3秒延迟重配 + dev_fb.c 强制全屏 + disp_lcd.c);board.dts/uboot-board.dts/config-5.4(LCD 部分);**kit board.dts 的 twi3 okay 改回 disabled** | /tmp/st7701sn_init、colorbar、雪花屏、fps≈65、frame[0,0,480,640] |
| 3 | 触摸 | dx_touch 修复版+Kconfig/Makefile;defconfig 加 evtest/i2c-tools | i2cdetect 0x38、evtest 坐标 |
| 4 | WiFi | sunxi-rfkill.c(module_platform_driver)+sunxi-wlan.c(minor 200);config-5.4 RTL8723DS=m 关 XR829;modules.mk(kmod 无 AUTOLOAD);defconfig(USES_REALTEK 等) | lsmod、wlan0、iw scan、udhcpc、ping |
| 5 | BT | config-5.4 RTL3WIRE 关 XR_BT_LPM;rtk_hciattach 锁版+btmanager 4.0.3+两个 Makefile;rc.local(先BT后WiFi);defconfig rtk_hciattach | /tmp/bt_boot.log、hci0 MTU1021、手机免PIN配对+放歌 |
| 6 | 扬声器 | 零代码改动(PA_SHDN 默认使能);aplay 测试 | 喇叭出声 |
| 7 | 汇总 | Tutorial_Doc/ZGL_BOARD_Tutorial.md | — |

## 关键坑(全部真实翻过车,参考包 README §五)

1. BT 先于 WiFi(8723ds 无 AUTOLOAD,rc.local 先 hciattach 再 modprobe)
2. 换固件/config 后必须断电 ≥15 秒(模块常供电无复位脚)
3. lcd_dclk_freq ≥ 28MHz;lcd_dsi_if 必须 0(video mode);hspw ≥ 4
4. uboot-board.dts 与 board.dts lcd0 时序必须一致(U-Boot 注入 boot_disp,旧几何→fb 半屏)
5. 触摸驱动必须修复版(复位序列末尾 gpio_direction_input)
6. bt_test 必须 start-stop-daemon 启动(防自拆 hci0)
7. 内核内建驱动改一次烧一次(不能只推模块)
8. libbtmg.so 必须 4.0.3(285460B;4.0.5 配对要 PIN);rtk_hciattach 必须 100096B 版(MTU 1021,手机才搜得到)

## 总耗时预估

| 阶段 | 写代码 | 编译 | 烧录+调试 | 一轮 |
|---|---|---|---|---|
| 1 串口 | ~2 min | ~5 min | ~10 min | ~20 min |
| 2 屏 | 拷贝+微调 ~10 min | ~10 min | ~20 min | ~40 min |
| 3 触摸 | ~5 min | ~10 min | ~15 min | ~30 min |
| 4 WiFi | ~10 min | ~15 min | ~20 min | ~45 min |
| 5 BT | ~15 min | ~15 min | ~30 min | ~1 h |
| 6 扬声器 | ~0 | 0 | ~10 min | ~15 min |
| 7 汇总 | ~30 min | — | — | ~30 min |
