# 06 · 扬声器(LM4871 功放 + 板载 codec)移植记录

> 状态:**✅ 已调通并实测通过**(A2DP 蓝牙放歌出声、板载 aplay 直推出声,**零代码改动**)
> 硬件:T113-S3 内置 audio codec → HPOUTR(Pin 98)→ LM4871 功放 → 喇叭
> 参考文档:原理图 `docs/sch/T113.pdf` / `T113.md`、原厂《Tina_Linux_蓝牙_开发指南》§音频输出。

## 1. 硬件链路(新原理图)

```
T113 audio codec (2030000.codec)
  ├─ DAC → HPOUTR (Pin 98) ──► LM4871 功放 ──► 喇叭
  └─ LM4871 PA_SHDN ──────── PB2
       硬件 10kΩ 下拉 → 默认使能(不接 MCU,无需软件控制)
```

关键硬件事实:
- **PA_SHDN=PB2 硬件下拉**,LM4871 上电即工作,**软件不需要 pa-pin/enable GPIO**;
- codec → LM4871 走 HPOUTR 单声道输出;
- 板级 `board.dts` 的 codec 节点、`asound.conf` 路由 SDK 已配好,本阶段**零代码改动**。

## 2. 状态核对(无需改动的证据)

| 项 | 状态 |
|---|---|
| codec 驱动 | `CONFIG_SUNXI_CODEC=y`(SDK 默认),`/proc/asound/cards` 见 `audiocodec` |
| 声卡 | `card 0: audiocodec,device 0: sunxi-snd-plat-aaudio-sunxi-snd-codec 2030000.codec-0` |
| 路由 | `/etc/asound.conf`:`pcm.!default → Playback → softvol → dmix(hw 48000 stereo)` |
| 开机路由 | `rc.local`:`amixer cset name="HPOUT Switch" 1` |
| 功放 | LM4871 PA_SHDN=PB2 硬件下拉,常使能 |

## 3. 测试与结果(adb 实测)

### 3.1 板载直推(跳过 asound,直接 hw)

```bash
# 推测试音(440Hz 正弦 3s,主机生成后 adb push)
adb push /tmp/sine.wav /tmp/sine.wav
adb shell 'aplay -D hw:audiocodec,0 /tmp/sine.wav'
# Playing WAVE '/tmp/sine.wav' : Signed 16 bit Little Endian, Rate 44100 Hz, Mono
# PCM 子设备: state: RUNNING → 播放完成后 closed
```

**实测:喇叭出"嘟——"测试音,清楚可闻。** 硬件链路 codec→HPOUTR→LM4871→喇叭全通。

### 3.2 蓝牙 A2DP 出声(复验,阶段 5 一起测)

手机连 `aw-bt-test-D1-A3` 放歌 → **喇叭出声、音质正常**(链路:
SBC(蓝牙) → bluealsa → asound Playback(dmix 48k stereo) → codec → LM4871 → 喇叭)。

### 3.3 完成判据

- [x] `aplay -D hw:audiocodec,0` 板载直推出声
- [x] A2DP 蓝牙放歌出声、音质正常
- [x] 零代码改动(纯硬件默认使能 + SDK 默认配置)

## 4. ⚠️ 音量控件默认值表(必读:反向标定,别按直觉改)

**sunxi codec 的音量控件是反向标定的(inverted TLV),amixer 值越大 = 寄存器值越小 = 音量越大。**
按"设最大值=最大音量"的直觉去改会踩坑(实测把音量打到 -74dB)。

`amixer -Dhw:audiocodec contents` 的驱动/DTS 默认值(**实测正常工作点,不要改**):

| numid | 控件名 | 默认值 | dB(标定) | 说明 |
|---|---|---|---|---|
| 16 | `DAC Digital Volume` | **63** | 0dB | **inverted**:`SOC_SINGLE_TLV(..., 0x3F, 1, digital_tlv)`,amixer 63=寄存器 0=0dB;amixer 0=寄存器 63=-74dB。范围 0-63 |
| 17 | `DACL Volume` | **160** | 0.75dB | 正向标定(`dac_vol_tlv`,step 0.75dB),寄存器=amixer 值。范围 0-255 |
| 18 | `DACR Volume` | **160** | 0.75dB | 同上 |
| 25 | `LINEOUT Volume` | 31 | 0dB 段 | range 0-31,段式标定 |
| 26 | `HPOUT Gain` | **7** | 0dB | inverted,范围 0-7(step 6dB) |
| 40 | `HPOUT Switch` | off→rc.local 设 on | — | 布尔开关 |
| — | `Soft Volume Master` | 255,255 | 0dB | asound softvol(-51~0dB),保持 255 |

对应的板级 DTS(`board.dts` codec 节点,驱动开机自动编程):

```
dac-vol = <0x3F>     → 驱动换算 dac_vol = 63 - dac-vol = 0   → DAC Digital 寄存器 0(0dB)
dacl-vol = <0xA0>    → 直接写 160 → DACL 寄存器 160(0.75dB)
dacr-vol = <0xA0>    → 直接写 160 → DACR 寄存器 160(0.75dB)
```

**踩坑记录(完整时间线)**:
1. 第一次 A2DP 测试(正常):混音器全默认,DAC Digital 显示 63,未动 → 寄存器 0 = 0dB ✓
2. 阶段 6 测试误设 `numid=16 → 0`(以为是开大)→ 寄存器 63 = **-74dB**,A2DP 变小声/失真
3. 第一次"修复"又设反(设 63→0 的反向)→ 依旧 -74dB
4. 直接读 codec 调试寄存器(`/sys/kernel/debug/regmap/...codec*/registers`)铁证:
   `0x00 DAC_DPC` 的 DVOL 位段 = 0x3f(63)= -74dB vs 0x0(0)= 0dB,才定位到反向标定
5. 恢复 **63** 后音质立即正常(与第一次逐位一致)

**验证寄存器的方法**(amixer 只显示控件值,不显示寄存器实际值,两个会不一样!):

```bash
adb shell 'R=/sys/kernel/debug/regmap/$(ls /sys/kernel/debug/regmap/ | grep -i codec | head -1)/registers; head -2 $R'
# 000: 80000000   ← DAC_DPC: DVOL 位段=0 → 0dB(正常态)
# 004: 0001a0a0   ← VOL_CTRL: 0xa0a0 = DACL/DACR=160(0.75dB)
```

### 4.1 常见故障速查

| 现象 | 原因 | 处理 |
|---|---|---|
| 完全无声 | `HPOUT Switch` off | `amixer -Dhw:audiocodec cset numid=40 on`(rc.local 开机已设) |
| 声音极小/失真 | `DAC Digital Volume` 被设成 0(= -74dB) | **设回 63**:`amixer -Dhw:audiocodec cset numid=16 63` |
| `Device or resource busy` | A2DP/bluealsa 占着声卡 | 先 `kill $(pidof bt_test)`,测完重新拉起(05-bt.md §5.2) |
| A2DP 卡顿不出声但链路在 | dmix/appl_ptr 卡死 | 重启 bt_test(05-bt.md §5.2),或重启整机 |

## 5. 涉及文件与改动

**零代码改动。** 以下仅为核对过的既有配置(未修改):

| 文件 | 内容 |
|---|---|
| `board.dts` codec 节点 | `dac-vol=<0x3F>`、`dacl-vol=<0xA0>`、`dacr-vol=<0xA0>` 等音量默认值 |
| `/etc/asound.conf`(rootfs) | `pcm.!default → Playback(softvol+dmix 48k stereo)` 路由 |
| `rc.local` | `amixer cset name="HPOUT Switch" 1`(开机路由) |

## 6. 下一步

阶段 7 汇总教程:`docs/Tutorial_Doc/ZGL_BOARD_Tutorial.md`,
仿 `T113_SDK.md` 结构,汇总 [01-uart.md](01-uart.md) ~ [06-audio.md](06-audio.md)。
