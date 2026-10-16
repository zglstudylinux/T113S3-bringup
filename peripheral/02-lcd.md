# 02 — MIPI 屏(D240N2501V1 / ST7701SN,480×640,2-lane)

> 状态: **✅ 已调通**(2026-10-15,镜像 md5 a10c80b9e39cbbba3587c1af27450059)  
> 平台: T113-S3 / Tina 5.0 V1.2 / Linux 5.4.61 / `t113-zgl_board`  
> 现象: 默认启动有 boot logo 显示,彩条/花屏/黑屏测试全部通过

---

## 1. 原理图与硬件

| 信号 | T113 引脚 | 说明 |
|---|---|---|
| MIPI DSI lane0 | PD0(P)/PD1(N) | 2 数据 lane |
| MIPI DSI lane1 | PD2(P)/PD3(N) | |
| MIPI DSI clk | PD4(P)/PD5(N) | |
| LCD 复位 | **PD20** | 低有效(MIPI_LCD_RST) |
| LCD TE | PD21 | 未用(video mode 不需要) |
| 背光 | **PD22(PWM7)** | MT9201 boost EN,R120 10k 下拉(默认灭) |

屏规格: D240N2501V1,2.4" 480×640 IPS,驱动 IC **ST7701SN**,MIPI DSI **2-lane video mode**(非 burst),RGB888。init 序列 44 条命令 + 5 个延时(源自 RK3566 D240C2501V1 参考设计)。

## 2. 核心坑(为什么这么配)

1. **`lcd_dclk_freq` 必须 ≥ 28MHz**:T113 的 combo DPHY PLL `m0` 只有 2 bit(最大分频 4),VCO 须 >1.3GHz → 每 lane 最低约 325Mbps。2-lane RGB888:`dclk×24/2 ≥ 325M` → **dclk ≥ 28MHz**。28×24/2=336Mbps(PLL m=4,干净无 WARN)。21MHz 时屏只亮背光无图像,`dmesg` 报 `pclk is too small`。
2. **`lcd_dsi_if` 必须 0(video mode 非 burst)**:burst 模式下 `loop_n1=(ht-x)×150/(dclk×8)-50` 在 ht=502/dclk=21 时为负 → u32 溢出 → DSI 指令引擎 DLY 卡死。
3. **`lcd_hspw ≥ 4`**:`dsi_hsa=hspw×3-10`,hspw=2 时为负。
4. **vt=850/vbp=62 给 DSI 带宽余量**:刷新率 ≈ 28MHz/(508×850) ≈ 65Hz,治顶部花屏。
5. **uboot-board.dts 与 board.dts 的 lcd0 时序必须一致**:U-Boot 即使 `lcd_used=0` 也会注入 boot_disp 参数,旧几何(280×1424)会让内核 fb 半屏。本板 U-Boot 侧 `lcd_used=<0>` + 参数同步新屏,双保险。
6. **开机 DSI 半初始化**:`disp_display.c` 补丁在 late_initcall 3 秒后做一轮 disable+enable,治每次开机 `fps < ref_fps`。
7. **fb 半屏**:`dev_fb.c` 补丁强制 layer 全屏几何(screen_win=480×640)+ addr=fb 缓冲,治 `screen_win=280x1424/addr=0` 残留。

## 3. 修改文件清单

### 3.1 内核新增(3 个)

| 文件 | 内容 |
|---|---|
| `kernel/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/lcd/st7701sn_d240n2501v1.c` | 面板驱动:open_flow(power_on→panel_init→tcon_enable→bl_open),44 条 DCS init(哨兵 0xFE/0xFC 避开 0xFF bank-switch),PD20 复位时序(高→低→高,间隔 5/10/120ms),完成后写 `/tmp/st7701sn_init` 验证标记 |
| `.../lcd/fl7707n_mipi.c` `.h` | 旧屏(280×1424 FL7707N)驱动残留,config 已关不编译,被注册文件引用保持自洽 |

### 3.2 内核修改(5 个)

| 文件 | 改动 |
|---|---|
| `.../disp/lcd/panels.c` | panel_array 注册 `&st7701sn_d240n2501v1_panel`(+fl7707n) |
| `.../disp/lcd/panels.h` | 加对应 extern 声明 |
| `.../disp/lcd/Kconfig` | 加 `config LCD_SUPPORT_ST7701SN_D240N2501V1` |
| `.../disp/Makefile` | 加 `disp-$(CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1) += lcd/st7701sn_d240n2501v1.o` |
| `.../disp/de/disp_display.c` | **必交**:late_initcall 3 秒后延迟 disable+enable(治开机 DSI 半初始化) |
| `.../disp/dev_fb.c` | **必交**:强制 fb 层全屏几何 + addr=fb 缓冲(治半屏) |
| `.../disp/de/disp_lcd.c` | 1 条 [LCD-DBG] 调试打印 |

### 3.3 板级

| 文件 | 改动 |
|---|---|
| `device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts` | `&lcd0` 切新屏:`lcd_driver_name="st7701sn_d240n2501v1"`、`lcd_dclk_freq=<28>`、`lcd_ht=<508>`、`lcd_hbp=<18>`、`lcd_hspw=<8>`、`lcd_vt=<850>`、`lcd_vbp=<62>`、`lcd_dsi_lane=<2>`、`lcd_dsi_if=<0>`、`lcd_pwm_ch=<7>`、`lcd_gpio_0=<&pio PD 20 GPIO_ACTIVE_LOW>`、`pinctrl-0=<&dsi2lane_pins_a>`;`&disp` 的 `fb0_width=<480>`/`fb0_height=<640`;新增 `backlight` 节点(pwm-backlight,pwms=<&pwm 7 10000 0>);twi3 保持 disabled(UART3 让路) |
| `device/config/chips/t113/configs/zgl_board/uboot-board.dts` | `lcd_used=<0>` + lcd0 参数同步新屏(U-Boot 不驱动面板,但注入 boot_disp 的参数必须与内核一致) |
| `device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4` | 加 `CONFIG_LCD_SUPPORT_ST7701SN_D240N2501V1=y`、`CONFIG_BACKLIGHT_CLASS_DEVICE=y`、`CONFIG_BACKLIGHT_PWM=y`、`CONFIG_BACKLIGHT_GPIO=y` |

说明:board.dts 里触摸(`dxts@38`)、`rfkill`、`&uart1` 在本阶段**暂禁用**(注释标注 phase-3/4/5 启用),保持每阶段最小风险面。

## 4. 编译打包

```bash
cd /home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2
. tools/scripts/setup-env.sh && source build/envsetup.sh && lunch t113-zgl_board
./build.sh kernel -j8   # 面板驱动内建, 改一次烧一次
./build.sh pack
# 产物: out/t113_linux_zgl_board_uart0.img (md5 a10c80b9…)
```

## 5. 测试与结果(2026-10-15 adb 实测)

### 5.1 面板初始化

```text
$ adb shell cat /tmp/st7701sn_init
OK 44 cmds 5 delays

$ adb shell dmesg | grep -iE 'st7701sn|pclk is too small|dsi|lcd0'
[    7.187697] disp 0, clk: pll(336000000),clk(336000000),dclk(28000000) dsi_rate(28000000)
[    7.187697]      clk real:pll(336000000),clk(336000000),dclk(42000000) dsi_rate(150000000)
[    7.244096] st7701sn: lcd_power_on
[    7.652422] st7701sn: lcd_panel_init begin
[    7.982473] st7701sn: init done, 44 cmds 5 delays
```

- `pclk is too small` 无输出 → dclk=28MHz 满足 DSI PHY 下限 ✓

### 5.2 framebuffer 与显示引擎

```text
$ adb shell ls -l /dev/fb0
crw-------    1 root     root       29,   0 Jan  1 08:01 /dev/fb0

$ adb shell cat /sys/class/graphics/fb0/bits_per_pixel
32                       ← XRGB8888

$ adb shell cat /sys/class/graphics/fb0/virtual_size
480,1280                 ← 480×640 双缓冲(2 个虚拟页)

$ adb shell cat /sys/class/disp/disp/attr/sys
screen 0:
de_rate 300000000 hz, ref_fps:65
mgr0: 480x640 fmt[rgb] cs[0x204] range[full] eotf[0x4] bits[8bits] err[0] ...
	lcd output	backlight( 50)	fps:65.3	 480x 640
	err:1	skip:13	irq:17895	vsync:0	vsync_skip:0	
   BUF    enable ch[1] lyr[0] z[16] ... fb[ 480, 640; 480, 640; 480, 640] \
     crop[ 0, 0, 480, 640] frame[ 0, 0, 480, 640] addr[ 0, 0, 0]
```

- `fps:65.3` ≈ `ref_fps:65` ✓(无半初始化,3 秒延迟重配补丁生效)
- `frame[0,0,480,640]` 全屏几何 ✓(dev_fb 补丁生效)
- 默认启动屏幕显示 boot logo ✓

### 5.3 显示功能测试

| 测试 | 命令 | 结果 |
|---|---|---|
| 花屏(雪花) | `adb shell "dd if=/dev/urandom of=/dev/fb0 bs=480k count=2"` | `2+0 records in/out`,屏上满屏雪花 ✓(写满整个 fb 缓冲;用 `cat /dev/urandom > /dev/fb0` 会在写满后报 `No space left on device`,属正常——fb 是定长缓冲) |
| 全黑 | `adb shell "dd if=/dev/zero of=/dev/fb0 bs=480k count=1"` | `1+0 records in/out`,屏全黑 ✓ |
| 彩条 | `adb shell "echo 8 > /sys/class/disp/disp/attr/colorbar"` | `exit=0`,屏上出 4 条彩条 ✓ |
| 关彩条 | `adb shell "echo 0 > /sys/class/disp/disp/attr/colorbar"` | `exit=0`,恢复正常显示 ✓ |

### 5.4 完成判据(全部通过)

- [x] 背光亮(PWM7),默认启动有 logo
- [x] `/tmp/st7701sn_init` = `OK 44 cmds 5 delays`
- [x] `dmesg` 无 `pclk is too small`
- [x] `fps:65.3` ≈ ref_fps:65,err 不增长
- [x] `frame[0,0,480,640]` 全屏
- [x] 花屏/黑屏/彩条测试通过

## 6. 失败排查对照表

| 症状 | 原因 | 处理 |
|---|---|---|
| 只有背光无图像 | dclk<28MHz | `dmesg \| grep pclk`,提到 28+ |
| 显示但 fps 远低于 ref_fps | 开机 DSI 半初始化 | 确认 disp_display.c 补丁在(3 秒延迟重配) |
| fb 内容只显示半屏 | U-Boot 注入旧几何 | 确认 uboot-board.dts lcd0 已同步 + dev_fb.c 补丁在 |
| 颜色异常 | 格式/极性 | 查 `lcd_dsi_format`(0=RGB888)、`lcd_frm` |
| 触摸区域错位 | 屏方向 | 阶段 3 处理(dx_touch 驱动内 X 镜像) |

## 7. 涉及文件汇总

内核 8 文件(3 新 + 5 改)+ 板级 3 文件,详见 §3;补丁导出:

- `docs/patches/kernel/0001-t113-zgl_board-add-ST7701SN-MIPI-panel-D240N2501V1-4.patch`
- `docs/patches/device/0001-t113-zgl_board-switch-lcd0-to-D240N2501V1-ST7701SN-M.patch`
