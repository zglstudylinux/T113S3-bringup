# 07 · 麦克风录音(GMI4015P 差分模拟麦,codec MIC3 通道)移植记录

> 状态:**✅ 已调通并实测通过**(`arecord` 录 5 秒 → `aplay` 回放,能正常听到自己的声音;**零代码改动**)
> 硬件:GMI4015P-2C-42db 差分模拟麦克风(MIC2 位号)→ codec MICIN3P/3N(差分)→ ADC3
> 参考文档:原理图 `docs/sch/T113.pdf` / `T113.md` §音频(页 5/6)、`/etc/asound.conf` CaptureMic 路由。

## 1. 硬件连接(新原理图)

```
GMI4015P-2C-42db 差分模拟麦克风(MIC2)        T113-S3 codec
┌──────────┐
│ 麦克风正极 │── C85 100nF ──► MICIN3P (Pin 87)   AVCC→R148 75Ω→R147 1kΩ 提供偏置
│ 麦克风负极 │── C86 100nF ──► MICIN3N (Pin 88)   R146 1kΩ 偏置至 AGND
└──────────┘        (C87 100pF 差分滤波)
```

关键硬件事实:
- **差分模拟麦,接 codec 的 MIC3 通道**(MICIN3P/MICIN3N 差分输入),不是 I2S 数字麦;
- 偏置网络全在硬件上(AVCC→R148 75Ω→R147 1kΩ 偏正极,R146 1kΩ 偏负极到 AGND);
- 板级 DTS 的 `adc3-*` 属性就是为这颗麦准备的,默认配置即工作。

## 2. 软件配置(全部已有,零改动)

| 层 | 配置 | 出处 |
|---|---|---|
| DTS | `adc3-vol = <160>`、`adc3-gain = <31>`(ADC3 = 麦克风通道) | `board.dts` codec 节点(1669/1673 行) |
| 驱动控件 | `MIC3 Input Select`、`MIC3 Switch`、`ADC3 Gain`(0-31,正向标定,step 1dB) | `snd_sun8iw20_codec.c:1022/1034` |
| 开机路由 | `rc.local`:`amixer -Dhw:audiocodec cset name='MIC3 Input Select' 1`、`MIC3 Switch 1`、`ADC3 Gain 19` | `base-files/etc/rc.local`(7-9 行) |
| ALSA 路由 | `pcm.CaptureMic`:hooks 设 Input1/2 Mux + LINEINL/R Switch + MIC3 选择 → dsnoop 16k 3ch → `ttable.0.2 1` 取第 3 路(ADC3) | `/etc/asound.conf`(69 行起) |

> **`MIC3 Input Select` 的语义**(驱动 `sunxi_mic3_enum`):值为 1 时 ADC3 采集 MIC3P/N 差分对。
> `ADC3 Gain 19`(≈19dB)是 rc.local 的实测工作点;DTS 默认 `adc3-gain=31`(31dB)偏大,
> 近讲说话建议 15~20,太大会削顶。

## 3. 测试与结果(adb 实测,用户执行)

```bash
# 录 5 秒,16kHz 单声道 S16(差分麦→ADC3,经 asound CaptureMic 路由)
adb shell 'arecord -d 5 -r 16000 -f S16_LE /tmp/test.wav'
# 回放(经 HPOUT→LM4871→喇叭)
adb shell 'aplay /tmp/test.wav'
```

**实测:录完回放,能正常听到录音内容。** 录放全链路:
`麦克风 → C85/C86 → MICIN3P/N → ADC3(PGA Gain) → 16kHz 采样 → wav 文件 → DAC → HPOUTR → LM4871 → 喇叭`

也可直推硬件设备(不经 asound 路由,验证 codec 本身):

```bash
adb shell 'arecord -D hw:audiocodec -d 3 -r 16000 -f S16_LE /tmp/mictest.wav'
adb shell 'aplay -D hw:audiocodec,0 /tmp/mictest.wav'
```

### 3.1 完成判据

- [x] `arecord` 16kHz 录音无报错,文件正常生成
- [x] `aplay` 回放能听到自己的声音
- [x] 零代码改动(DTS/驱动/rc.local/asound.conf 全部默认)

## 4. 常见问题速查

| 现象 | 原因 | 处理 |
|---|---|---|
| 录出来全是静音 | `MIC3 Switch` 没开 / `MIC3 Input Select` 不为 1 | `amixer -Dhw:audiocodec cset name='MIC3 Switch' 1`;`cset name='MIC3 Input Select' 1`(rc.local 开机已设) |
| 声音太小 | `ADC3 Gain` 低 | `amixer -Dhw:audiocodec cset name='ADC3 Gain' 19`(近讲实测工作点,范围 0-31) |
| 声音破/削顶 | `ADC3 Gain` 太大 | 降到 15 左右;或检查 `ADC3 Volume`(DTS 默认 160) |
| 回放无声 | 见 06-audio.md §4.1 | 播放侧问题,查 DAC/HPOUT |
| `Device or resource busy` | A2DP 占着声卡(dmix 录音侧是 dsnoop,通常不冲突) | 先 `kill $(pidof bt_test)` 再录 |

> 注意:录音控件(`ADC3 Gain`/`ADC1-3 Volume`)是**正向标定**(step +1dB / +0.75dB),
> 和播放侧的 `DAC Digital Volume`(反向)不一样,别混淆。

## 5. 涉及文件与改动

**零代码改动。** 以下为核对过的既有配置(未修改):

| 文件 | 内容 |
|---|---|
| `board.dts` codec 节点 | `adc3-vol=<160>`、`adc3-gain=<31>`(注释即"接麦克风") |
| `rc.local` | MIC3 Input Select=1、MIC3 Switch=1、ADC3 Gain=19 |
| `/etc/asound.conf` | `CaptureMic` hooks + dsnoop 16k 3ch,ttable 取 ADC3 路 |

## 6. 下一步

阶段 7 汇总教程:`docs/Tutorial_Doc/ZGL_BOARD_Tutorial.md`,
汇总 [01-uart.md](01-uart.md) ~ [07-mic.md](07-mic.md) 全部外设 bring-up 记录。
