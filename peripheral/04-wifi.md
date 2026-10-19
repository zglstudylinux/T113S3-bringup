# 04 · WiFi(RTL8723DS,SDIO)移植记录

> 状态:**✅ 已调通并实测通过**(扫描 → 连接热点 → DHCP 拿 IP → ping 网关/外网)
> 模块:RTL8723DS(2.4G WiFi + BT 二合一,SDIO 接 WiFi,UART 接 BT)
> 本篇只讲 WiFi;蓝牙在 [05-bt.md](05-bt.md)。
> 参考文档:`docs/datasheet/` 下的 Tina Wi-Fi 开发指南、Wi-Fi 模组移植指南。

## 1. 硬件连接(新原理图 `docs/sch/T113.pdf` / `T113.md`)

```
RTL8723DS 模块(板载,常供电)         T113-S3
┌──────────────┐
│ SDIO CLK/DMD │◄──── PG0~PG5 (SDC1, wlan_busnum=1)
│ WL_REG_ON    │───── 上拉(常使能,不接 MCU)  ← 原理图 WL_DIS# 拉高
│ BT_REG_ON    │───── 上拉(常使能,不接 MCU)  ← 原理图 BT_DIS# 拉高
│ UART BT      │───── UART1 四线 PG6(PG7/PG8/PG9)(阶段 5 用)
│ 32K          │───── 模块内部自带,不接 PG11
└──────────────┘
```

关键硬件事实(决定软件写法):
- **无 regon / hostwake GPIO**:模块 WL_DIS#/BT_DIS# 硬件拉高常供电,驱动里**不需要** `wlan_regon`/`wlan_hostwake` 属性;
- **无 32K 时钟输出**:模块内部自带 32K,`board.dts` 里不配 `32k-fanout1`(配了反而会因 ccu devlink 依赖让 probe 永远 defer);
- WiFi 走 SDIO 总线 1(`wlan_busnum = <0x1>`)。

## 2. 改动总览

| # | 文件 | 改动 |
|---|---|---|
| 1 | `kernel/linux-5.4/drivers/misc/sunxi-rf/sunxi-rfkill.c` | `_probe` 注册 → 普通注册(必改,否则 probe 永久失败) |
| 2 | `kernel/linux-5.4/drivers/misc/sunxi-rf/sunxi-wlan.c` | misc minor 动态 → 静态 200(必改,否则 -EBUSY) |
| 3 | `device/.../linux-5.4/board.dts` | rfkill@0 `disabled` → `okay` |
| 4 | `device/.../linux-5.4/config-5.4` | `CONFIG_XR829_WLAN=m` → `CONFIG_RTL8723DS=m` |
| 5 | `openwrt/target/t113/t113-common/modules.mk` | 新增 `kmod-net-rtl8723ds` 包(**故意无 AUTOLOAD**) |
| 6 | `openwrt/target/t113/t113-zgl_board/defconfig` | 开 rtl8723ds 内核包+固件,关 xr829 系 |

> `board.dts` 的 rfkill/wlan 节点本身在阶段 2(LCD)时就已按 kit 版带入,
> 当时留 `disabled`;本阶段只把 status 翻成 `okay`。

## 3. 每处修改的 diff

### 3.1 内核:`sunxi-rfkill.c` — 修复 probe 永久注销

`kernel/linux-5.4/drivers/misc/sunxi-rf/sunxi-rfkill.c`(文件末尾)

```diff
--- a/drivers/misc/sunxi-rf/sunxi-rfkill.c
+++ b/drivers/misc/sunxi-rf/sunxi-rfkill.c
@@ -191,7 +191,16 @@ static struct platform_driver rfkill_driver = {
 	},
 };
 
-module_platform_driver_probe(rfkill_driver, rfkill_probe);
+/*
+ * zgl_board: use normal registration instead of module_platform_driver_probe.
+ * The *_probe() variant sets prevent_deferred_probe and unregisters the driver
+ * if no device binds during init (e.g. when the rfkill@0 node is still waiting
+ * for its pinctrl/ccu suppliers). That permanently kills sunxi_wlan_init, so
+ * sunxi_wlan_get_bus_index() returns -EINVAL and the rtl8723ds driver fails
+ * with "get wifi_sdc_id failed" / "power on failed". Normal registration lets
+ * the device bind (and defer) properly.
+ */
+module_platform_driver(rfkill_driver);
 
 MODULE_DESCRIPTION("sunxi rfkill driver");
 MODULE_LICENSE("GPL");
```

**为什么必改**:`module_platform_driver_probe()` 在驱动注册时如果没绑到任何设备
(rfkill@0 还在等 pinctrl/ccu supplier),会设置 `prevent_deferred_probe` 并**直接注销驱动**。
后果:8723ds 模块加载时 `sunxi_wlan_get_bus_index()` 返回 -EINVAL,
报 `get wifi_sdc_id failed` / `power on failed`,WiFi 永远起不来。
普通 `module_platform_driver()` 允许设备正常 defer,等 supplier 就绪后再 bind。

### 3.2 内核:`sunxi-wlan.c` — 静态 misc minor

`kernel/linux-5.4/drivers/misc/sunxi-rf/sunxi-wlan.c`

```diff
--- a/drivers/misc/sunxi-rf/sunxi-wlan.c
+++ b/drivers/misc/sunxi-rf/sunxi-wlan.c
@@ -246,8 +246,15 @@ static struct attribute_group misc_attribute_group = {
 	.attrs = misc_attributes,
 };
 
+/*
+ * zgl_board: use a static minor (200) instead of MISC_DYNAMIC_MINOR.
+ * On this platform the dynamic-minor bitmap can be exhausted by leaked
+ * registrations during boot (ion ends up at minor 127 = the last free slot),
+ * so misc_register() for "sunxi-wlan" returned -EBUSY and sunxi_wlan_init()
+ * aborted, leaving wlan_data == NULL.  Minor 200 is free (see /proc/misc).
+ */
 static struct miscdevice sunxi_wlan_dev = {
-	.minor = MISC_DYNAMIC_MINOR,
+	.minor = 200,
 	.name  = "sunxi-wlan",
 };
```

**为什么必改**:这台板子开机时动态 minor 位图被用光(ion 排到 127 = 最后一个空位),
`misc_register("sunxi-wlan")` 返回 -EBUSY,`sunxi_wlan_init()` 中止,`wlan_data` 一直是 NULL,
之后 `wlan_dev_open`/`WLAN_IOC_CMD_POWER_ON` 全部失败。改静态 200(/proc/misc 中空闲)一次解决。
**实测验证**:开机后 `cat /proc/misc` 可见 `200 sunxi-wlan`,`/dev/sunxi-wlan` 正常出现。

### 3.3 设备树:使能 rfkill

`device/config/chips/t113/configs/zgl_board/linux-5.4/board.dts`(约 1094 行)

```diff
--- a/configs/zgl_board/linux-5.4/board.dts
+++ b/configs/zgl_board/linux-5.4/board.dts
@@ -1095,7 +1095,7 @@
 		compatible    = "allwinner,sunxi-rfkill";
 		chip_en;
 		power_en;
-		status        = "disabled";   /* phase-4 (WiFi) will enable */
+		status        = "okay";
 		/* No pinctrl-0 here: it created a devlink dependency on the pinctrl
 		 * driver that could defer the probe indefinitely (device never
 		 * attached at boot). PG11(32K) is not wired on this board anyway. */
```

rfkill/wlan 节点完整内容(阶段 2 已带入,这里给对照,方便手动移植):

```dts
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

### 3.4 内核配置:换 WiFi 驱动

`device/config/chips/t113/configs/zgl_board/linux-5.4/config-5.4`(99 行)

```diff
--- a/configs/zgl_board/linux-5.4/config-5.4
+++ b/configs/zgl_board/linux-5.4/config-5.4
@@ -96,7 +96,7 @@ CONFIG_BLK_DEV_SD=y
 CONFIG_NETDEVICES=y
 CONFIG_SUNXI_GMAC=y
 CONFIG_USB_USBNET=y
-CONFIG_XR829_WLAN=m
+CONFIG_RTL8723DS=m
 CONFIG_INPUT_EVDEV=y
 CONFIG_INPUT_SENSORINIT=y
 CONFIG_KEYBOARD_SUNXI=y
```

> RTL8723DS 驱动源码在 `kernel/linux-5.4/drivers/net/wireless/rtl8723ds/`
> (SDK 自带),经 `drivers/net/wireless/Kconfig:53` 和 `Makefile:36` 挂进构建,
> 编译产物为 `8723ds.ko`(≈2.1MB,内嵌 WiFi 固件,无外部固件依赖)。

### 3.5 OpenWrt:新增 kmod 包(无 AUTOLOAD!)

`openwrt/target/t113/t113-common/modules.mk`(在 `net-rtl8821cs` 之后插入)

```diff
--- a/t113-common/modules.mk
+++ b/t113-common/modules.mk
@@ -252,6 +252,22 @@ endef
 
 $(eval $(call KernelPackage,net-rtl8821cs))
 
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
+
 define KernelPackage/sunxi-disp
   SUBMENU:=$(VIDEO_MENU)
   TITLE:=sunxi-disp support
```

**关键设计:故意不写 `AUTOLOAD` 行。**
8723ds 的 WiFi SDIO 固件下载流程会破坏模块 BT 侧的 ROM 下载状态:
如果 WiFi 先于蓝牙初始化,之后 `rtk_hciattach` 会一直 H5 sync timeout。
所以开机时序必须是 **BT 先起(rtk_hciattach)→ 再 `insmod 8723ds.ko`**,
这个顺序由 `/etc/rc.local`(阶段 5)控制。本阶段测试阶段用 `insmod` 手动加载。

### 3.6 OpenWrt:defconfig 包选择

`openwrt/target/t113/t113-zgl_board/defconfig`(4 处)

```diff
--- a/t113-zgl_board/defconfig
+++ b/t113-zgl_board/defconfig
@@ -3193,8 +3193,7 @@
-# CONFIG_XR829_USE_40M_SDD is not set
-CONFIG_USES_XR829=y
+CONFIG_USES_REALTEK=y
@@ -3274,7 +3273,7 @@
-# CONFIG_PACKAGE_rtl8723ds-firmware is not set
+CONFIG_PACKAGE_rtl8723ds-firmware=y
@@ -3291,7 +3290,7 @@
-CONFIG_PACKAGE_xr829-firmware=y
+# CONFIG_PACKAGE_xr829-firmware is not set
@@ -4070,10 +4069,11 @@
+CONFIG_PACKAGE_kmod-net-rtl8723ds=y
-CONFIG_PACKAGE_kmod-net-xr829=y
+# CONFIG_PACKAGE_kmod-net-xr829 is not set
@@ -6982,7 +6982,7 @@
-CONFIG_PACKAGE_xr829-rftest=y
+# CONFIG_PACKAGE_xr829-rftest is not set
```

> `rtl8723ds-firmware` 装的是 **蓝牙 H4 固件**
> (`/lib/firmware/rtlbt/rtl8723dsh4_fw` + `rtl8723dsh4_config`,供阶段 5
> rtk_hciattach 用;WiFi 固件内嵌在 8723ds.ko 里,不依赖该包)。
> 源文件:`platform/allwinner/wireless/firmware/rtl8723ds/`。

## 4. 编译

```bash
cd /home/zgl/Linux_SDK/T113/T113-Tina5.0-V1.2
. tools/scripts/setup-env.sh
source build/envsetup.sh
lunch t113-zgl_board

./build.sh kernel -j8          # rfkill/wlan 内建 + 8723ds.ko (~3 min 增量)
./build.sh openwrt_rootfs -j1  # kmod-net-rtl8723ds + rtl8723ds-firmware (~10 min)
./build.sh pack                # (~1.5 min)
# 产物: out/t113_linux_zgl_board_uart0.img
```

**产物自检**(烧录前可在主机确认,无需上板):

```bash
# 8723ds.ko 和蓝牙固件都应出现在 rootfs 里
unsquashfs -l out/t113/zgl_board/openwrt/rootfs.img | grep -E 'rtlbt|8723ds.ko'
# squashfs-root/lib/firmware/rtlbt/rtl8723d_config
# squashfs-root/lib/firmware/rtlbt/rtl8723d_fw
# squashfs-root/lib/firmware/rtlbt/rtl8723dsh4_config
# squashfs-root/lib/firmware/rtlbt/rtl8723dsh4_fw
# squashfs-root/lib/modules/5.4.61/8723ds.ko
```

本次镜像:`21022720` 字节,MD5 `1e90397e953a80b446778124ed46b977`。

## 5. 测试与结果(adb 实测)

> ⚠️ **换固件后建议彻底断电 ≥15s 再上电**:RTL8723DS 常供电,
> flash 状态残留可能影响 SDIO 枚举/后续 BT。

### 5.1 开机自动部分(无需手动)

```bash
adb shell dmesg | grep mmc1
# [    2.928544] mmc1: new high speed SDIO card at address 0001   ← 开机 2.9s 即枚举到

adb shell cat /proc/misc | grep -E 'wlan|ion'
# 200 sunxi-wlan        ← 静态 minor 200 生效
# 127 ion

adb shell ls /dev/sunxi-wlan
# crw------- 1 root root 10, 200 ...    ← 字符设备存在

adb shell ls /sys/bus/platform/drivers/ | grep sunxi-rfkill
# sunxi-rfkill                          ← rfkill probe 成功绑定
```

### 5.2 手动加载 WiFi 模块(阶段 5 后由 rc.local 自动)

```bash
adb shell insmod /lib/modules/5.4.61/8723ds.ko
adb shell lsmod
# 8723ds               2150400  0
```

关键 dmesg:

```
[  102.201032] RTW: rtl8723d_FirmwareDownload fw: FW_NIC, size: 29262
[  102.201042] RTW: rtl8723d_FirmwareDownload: fw_ver=31 fw_subver=0000 sig=0x23d1 ...
[  102.695851] RTW: polling_fwdl_chksum: Checksum report OK! (1, 0ms) ...
[  102.739285] RTW: rtl8723d_FirmwareDownload success. write_fw:1, 540ms
[  102.929917] RTW: rtw_macaddr_cfg mac addr:34:75:63:28:4d:23
[  103.009922] RTW: rtw_ndev_init(wlan0) if1 mac_addr=34:75:63:28:4d:23
[  103.089382] RTW: module init ret=0
```

```bash
adb shell ip link set wlan0 up
adb shell iw dev | grep -A3 wlan0
# Interface wlan0
#     ifindex 4
#     addr 34:75:63:28:4d:23
```

### 5.3 扫描

```bash
adb shell iw dev wlan0 scan | grep -E 'freq:|SSID:' | head -12
# freq: 2412  SSID: Bluetrum-14F
# freq: 2437  SSID: Bluetrum-14F        (以及 guest / zq / 笔记本热点,共 12 个 BSS)
```

### 5.4 连接热点 → DHCP → ping(实测通过)

```bash
adb shell 'kill $(pidof wpa_supplicant); mkdir -p /var/run/wpa_supplicant
cat > /tmp/wpa_test.conf <<EOF
ctrl_interface=/var/run/wpa_supplicant
update_config=1
network={
    ssid="zgl"
    psk="88888888"
    key_mgmt=WPA-PSK
}
EOF
wpa_supplicant -B -i wlan0 -c /tmp/wpa_test.conf
sleep 8
wpa_cli -i wlan0 status'
# ssid=zgl
# wpa_state=COMPLETED          ← 4-way 握手成功
# pairwise_cipher=CCMP
# group_cipher=CCMP
# ip_address=10.25.28.164

adb shell udhcpc -i wlan0 -n -q
# udhcpc: lease of 10.25.28.164 obtained, lease time 3599
# setting default routers: 10.25.28.129

adb shell ping -c 4 10.25.28.129        # 网关: 0% loss
# 64 bytes from 10.25.28.129: seq=0 ttl=64 time=65.647 ms
# --- 4 packets transmitted, 4 packets received, 0% packet loss

adb shell ping -c 3 223.5.5.5           # 外网: 0% loss
# --- 3 packets transmitted, 3 packets received, 0% packet loss

adb shell iw dev wlan0 link
# Connected to 62:e4:47:9d:d6:12 (on wlan0)
#     SSID: zgl
#     freq: 2412
#     signal: -60 dBm
#     tx bitrate: 72.2 MBit/s
```

### 5.5 完成判据

- [x] `/proc/misc` 有 `200 sunxi-wlan`,`/dev/sunxi-wlan` 存在
- [x] `mmc1` 开机枚举出 SDIO 卡
- [x] `insmod 8723ds.ko` 成功,固件下载 OK,`wlan0` 出现
- [x] `iw scan` 扫到周围 AP
- [x] `wpa_cli status` → `COMPLETED`
- [x] `udhcpc` 拿到 IP,`ping` 网关/外网 0% loss

### 5.6 排错记录(实际踩过的坑)

**现象**:连办公室 AP `Bluetrum-14F` 时 `auth success → assoc success`,
但 4-way 握手循环失败 14 次,AP 每次 deauth reason=2:

```
wlan0: WPA: 4-Way Handshake failed - pre-shared key may be incorrect
wlan0: CTRL-EVENT-SSID-TEMP-DISABLED ... reason=WRONG_KEY
```

**结论**:就是密码不对(当时用猜的 12345678),**不是驱动问题**。
换正确密码的 AP(zgl 热点)一次握手成功。
排查方法:`wpa_supplicant -dd` 看日志里
`pre-shared key may be incorrect` / `WRONG_KEY`,同时确认密码与加密套件:
该 AP 是 `Group cipher: TKIP`,驱动已正确协商(GTK TKIP + PTK CCMP),排除兼容性问题。

## 6. 涉及文件

| 仓库 | 文件 |
|---|---|
| kernel/linux-5.4 | `drivers/misc/sunxi-rf/sunxi-rfkill.c`、`sunxi-wlan.c` |
| device/config/chips/t113 | `configs/zgl_board/linux-5.4/board.dts`、`config-5.4` |
| openwrt/target/t113 | `t113-common/modules.mk`、`t113-zgl_board/defconfig` |

手动移植方式:按上文 diff 手改(修改哪个文件、改哪一行都已标注)。

## 7. 下一步

蓝牙(H5 协议,走 UART1 PG6-9)在 [05-bt.md](05-bt.md):
`CONFIG_BT_HCIUART_RTL3WIRE=y` + 锁版 `rtk_hciattach` + rc.local **先 BT 后 WiFi** 时序。
