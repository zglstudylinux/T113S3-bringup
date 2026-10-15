# T113S3-bringup — zgl_board 外设调试教程仓库

> 硬件: Allwinner T113-S3,自研底板 zgl_board
> SDK: Tina 5.0 V1.2 (Linux 5.4.61)
> 原理图: `sch/T113.pdf`(7页,权威) + `sch/T113.md`(引脚分析,权威)

## 目录结构

```
docs/
├── README.md                  ← 本文件
├── plan/
│   ├── plan_01.md             ← 已作废(旧原理图时代的 UART→屏→触摸 计划)
│   └── plan_02.md             ← 现行计划(新原理图 + porting_kit,七阶段)
├── peripheral/                ← ★ 每调通一个外设一篇文档
│   ├── 01-uart.md             ← 阶段1: 调试串口 UART3(PB6/PB7)
│   ├── 02-lcd.md              ← 阶段2: MIPI 屏 D240N2501V1(ST7701SN, 480x640)
│   ├── 03-touch.md            ← 阶段3: 触摸 dx_touch(I2C 0x38)
│   ├── 04-wifi.md             ← 阶段4: WiFi RTL8723DS(SDIO)
│   ├── 05-bt.md               ← 阶段5: 蓝牙 RTL8723DS(UART1 H5)
│   └── 06-audio.md            ← 阶段6: 扬声器(LM4871 + HPOUTR)
├── patches/                   ← 每阶段导出的内核/设备树补丁(按组件)
├── sch/                       ← 原理图 PDF 与引脚分析
│   ├── T113.pdf
│   └── T113.md
├── Tutorial_Doc/              ← 最终汇总教程(仿 T113_SDK.md 结构)
│   ├── T113_SDK.md            ← 原始参考教程
│   └── ZGL_BOARD_Tutorial.md  ← 本轮产出的汇总教程
├── datasheet/                 ← 官方文档(大文件,不入库,见 .gitignore)
├── touch_screen/              ← 触摸评估与驱动源码参考
├── test/                      ← 测试脚本
├── BUILD_T113_ZGL_BOARD.md    ← 编译实战文档
└── SYNC_FROM_BARE_REPO.md     ← SDK 拉取指南
```

## 工作流

每阶段循环: **改代码 → 编译出新镜像 → 烧录 → adb/串口验证 → 写 peripheral/NN-xxx.md → 组件仓库 commit + patch 导出到本仓库 → push**。

SDK 内的组件仓库(device/kernel/bsp/openwrt)各自提交;本仓库只放文档、补丁和截图。

## 关键结论(为什么这么做)

| 约束 | 原因 |
|---|---|
| UART3 在 PB6/PB7 | 原理图 U24 调试座;PB6/PB7 原被 twi3/wlan_regon/bt_rst 占用,全部让路 |
| lcd_dclk_freq ≥ 28MHz | T113 DSI PHY PLL 硬下限(每 lane ≥325Mbps);21MHz 屏只亮背光 |
| 触摸驱动用修复版 | 屏厂原版复位序列缺 `gpio_direction_input(irq_pin)`,IRQ 注册必失败 |
| WiFi/BT 模块常供电无复位脚 | WL_DIS#/BT_DIS# 硬件上拉不接 MCU;换固件后必须断电 ≥15 秒 |
| BT 先于 WiFi | WiFi SDIO 固件下载会破坏模块 BT 的 ROM 下载状态;8723ds.ko 不 AUTOLOAD,rc.local 先 hciattach 再 modprobe |
| LM4871 功放默认使能 | PA_SHDN(PB2) 硬件 100k 下拉;高电平关断,无需 pa-pin |

## 参考包

`/home/zgl/Linux_SDK/T113/zgl_board_peripheral_porting_kit/`(已验证可用的源码树导出,含锁版二进制 md5 表)。
