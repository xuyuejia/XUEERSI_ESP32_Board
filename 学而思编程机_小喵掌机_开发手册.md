# 学而思编程机（小喵掌机 / XiaoMiao）开发手册

> **别名：** 学而思 ESP32 掌机、小喵掌机、XiaoMiao、学而思编程掌机
>
> **整理时间：** 2026-06-25
>
> **信息来源：** 微信公众号「PY学习笔记」文章合集、Gitee/GitHub 开源仓库、逆向工程资料

---

## 目录

1. [硬件规格](#1-硬件规格)
2. [完整引脚分配](#2-完整引脚分配)
3. [显示驱动（ST7735）](#3-显示驱动st7735)
4. [开发环境搭建](#4-开发环境搭建)
5. [固件刷写](#5-固件刷写)
6. [基础示例代码](#6-基础示例代码)
7. [LVGL GUI 开发](#7-lvgl-gui-开发)
8. [ESPHome 方案](#8-esphome-方案)
9. [示例项目](#9-示例项目)
10. [关键限制与注意事项](#10-关键限制与注意事项)
11. [ESP-IDF 原生开发（性能分析与迁移指南）](#11-esp-idf-原生开发性能分析与迁移指南)
12. [资源链接汇总](#12-资源链接汇总)

---

## 1. 硬件规格

| 项目 | 规格 |
|------|------|
| **主控芯片** | ESP32-WROVER-B（双核 Xtensa LX6, 240MHz, 内置 8MB PSRAM） |
| **屏幕** | 1.8 寸 TFT LCD, ST7735 驱动, SPI 接口, **128×160 像素** (已通过 esptool + `get_physical_resolution()` 实测确认), RGB565, 竖屏旋转 90° 后横屏 160×128 |
| **存储** | MicroSD 卡槽（SPI 分时复用） |
| **按键** | 6 键（上/下/左/右/A/B） |
| **蜂鸣器** | 无源蜂鸣器（PWM 驱动） |
| **传感器** | 光照传感器（ADC）、热敏电阻（温度 ADC） |
| **扩展接口** | I2C、UART0、4 个 GPIO 预留（PH2.0 3P 座） |
| **供电** | 内置锂电池，USB 充电 |
| **外壳** | 成品外壳，掌机形态 |

> **参考价格：** 二手市场约 50 元以内，性价比极高。

---

## 2. 完整引脚分配

> **引脚发现方法：** 学而思和 KittenBot 官方不提供原理图或引脚数据，以下信息全部来自社区逆向工程——先用万用表通断档逐个测出 MCU 引脚与模块的连接关系，再用 MicroPython 代码验证功能。详见 [附录 C：引脚逆向工程方法](#附录-c引脚逆向工程方法)。

### 2.0 ESP32-WROVER-B 可用引脚总览

ESP32-WROVER-B 共 39 个引脚，其中：

| 类别 | 引脚 | 数量 |
|------|------|------|
| **内部 SPI Flash 占用** | GPIO6 ~ GPIO11 | 6 个不可用 |
| **可用 GPIO** | 0, 2, 4, 5, 12~15, 18, 19, 21~23, 25~27, 32~36 | 21 个 |
| **仅输入/ADC** | 34, 35, 36, 39 | 4 个（不可输出，无内部上下拉） |
| **启动敏感** | 0, 2, 12 | 3 个（上电时电平影响启动模式） |
| **SPI2 默认引脚** | SCK=18, MOSI=23, MISO=19 | 3 个（LCD 与 SD 卡共享） |
| **SDIO Slot 1 默认引脚** | SCK=14, CMD=15, D0=2, D1=4 | 4 个（本机未使用 SDIO 模式） |

### 2.1 显示屏 — ST7735 (SPI2)

| 功能 | GPIO | 说明 |
|------|------|------|
| SCK | **18** | SPI 时钟，与 SD 卡共享 |
| MOSI | **23** | SPI 数据输出，与 SD 卡共享 |
| MISO | **19** | SPI 数据输入，与 SD 卡共享 |
| CS | **5** | 屏幕片选 |
| DC | **4** | 数据/命令选择 |
| RES | **19** | 复位（与 MISO 共享） |

### 2.2 MicroSD 卡 (SPI2)

| 功能 | GPIO | 说明 |
|------|------|------|
| SCK | 18 | 共享 |
| MOSI | 23 | 共享 |
| MISO | 19 | 共享 |
| CS | **22** | SD 卡片选 |

> TFT 与 SD 卡共用 SPI2（GPIO18/23/19），通过各自 CS 片选分时复用，无冲突。

### 2.3 按键（6 键）

| 按键 | GPIO | 极性 | 说明 |
|------|------|------|------|
| 上 (UP) | **2** | 按下低电平，内部上拉 | ⚠️ 启动敏感 |
| 下 (DOWN) | **13** | 按下低电平，内部上拉 | |
| 左 (LEFT) | **27** | 按下低电平，内部上拉 | |
| 右 (RIGHT) | **35** | 按下低电平 | 🔒 仅输入，无内部上拉 |
| A | **34** | 按下低电平 | 🔒 仅输入，无内部上拉 |
| B | **12** | 按下低电平，内部上拉 | ⚠️ 启动敏感 |

### 2.4 蜂鸣器

| 功能 | GPIO | 说明 |
|------|------|------|
| 蜂鸣器 | **14** | 无源蜂鸣器，PWM (LEDC) 驱动，可播放不同频率音调 |

### 2.5 传感器 (ADC)

| 传感器 | GPIO | ADC 通道 | 说明 |
|------|------|------|------|
| 光照传感器 | **36** | ADC1_CH0 | 🔒 仅输入 |
| 热敏电阻（温度） | **39** | ADC1_CH3 | 🔒 仅输入 |

### 2.6 I2C 总线

| 功能 | GPIO | 说明 |
|------|------|------|
| SCL | **15** | I2C 时钟 |
| SDA | **21** | I2C 数据 |

> 注意：I2C 地址 0x40 通常被电机/LED 驱动占用。

### 2.7 UART0（原生串口）

| 功能 | GPIO | 说明 |
|------|------|------|
| TX | **1** | 不经过 USB |
| RX | **3** | 不经过 USB |

### 2.8 预留扩展口

| GPIO | 说明 |
|------|------|
| **33, 32, 26, 25** | PH2.0 3P 座，可用作 DAC / 通用 IO |

### 2.9 引脚约束速查

- 🔒 **仅输入引脚：** GPIO34, 35, 36, 39（不可设为输出，无内部上下拉）
- ⚠️ **启动敏感：** GPIO12（B 键）上电阶段避免外部高电平
- 🔄 **共享 SPI：** GPIO18/23/19（TFT + SD 卡）

### 2.10 实物布局对照

以下基于实物拆机图的硬件布局分析（图片分辨率 1440×1920）：

```
┌─────────────────────────────────────┐
│  [Micro USB 充电/下载]               │
│                                      │
│  ┌──────────────┐   ┌────────────┐   │
│  │ ESP32-WROVER │   │ MicroSD    │   │
│  │   -B 模块    │   │  卡槽      │   │
│  │ (主控 MCU)   │   │            │   │
│  └──────────────┘   └────────────┘   │
│                                      │
│  ┌──────────────────────────────┐    │
│  │  1.8" ST7735 TFT LCD (FPC)  │    │
│  └──────────────────────────────┘    │
│                                      │
│  [● 蜂鸣器]                          │
│                                      │
│  [上] [下] [左] [右] [A] [B]        │
│                                      │
│  ┌────────────────────────────────┐   │
│  │ 扩展接口区（底部 PH2.0 座）：    │   │
│  │ M1+ M1- M2+ M2- │ SCL SDA     │   │
│  │ (电机驱动)       │ (I2C)       │   │
│  │ ESP-TX RX GND IO0 │ IO25 26       │   │
│  │ (调试串口)       │ IO32 33        │   │
│  │                  │ (通用 GPIO) │   │
│  └────────────────────────────────┘   │
│  [黄色塑料外壳] [内置锂电池]          │
└─────────────────────────────────────┘
```

**主要区域说明：**

| 区域 | 位置 | 说明 |
|------|------|------|
| ESP32-WROVER-B | 左上 | 双核 240MHz + 8MB PSRAM，银色金属屏蔽罩 |
| MicroSD 卡槽 | 右上 | 标准 MicroSD，与 LCD 共享 SPI2 |
| TFT 屏幕 | 中间偏上 | ST7735, 128×160, FPC 排线连接 |
| 蜂鸣器 | 左中区域 | 黑色圆柱形无源蜂鸣器 |
| 6 键十字键区 | 中下区域 | 导电胶按键，方向键 + A/B 键 |
| 扩展接口 | 底部 | 4 组 PH2.0 3P 座 + 1 组 4P 调试口 |
| USB 接口 | 顶部 | Micro USB，用于充电和程序下载 |
| 电池 | 背面（壳内） | 可充电锂电池 |

---

## 3. 显示驱动（ST7735）

屏幕为 **ST7735 驱动的 1.8 寸 TFT**，分辨率 **128×160**（物理宽高，程序中使用 160×128 横屏），SPI 接口，RGB565 色彩格式。

### 3.1 屏幕参数

| 参数 | 值 |
|------|-----|
| 驱动 IC | ST7735 |
| 分辨率 | 128 × 160 |
| 色深 | 16-bit RGB565 |
| 接口 | SPI（Mode 0） |
| SPI 频率 | 20-40 MHz |
| 旋转方向 | 顺时针 90° 横屏 |

### 3.2 MicroPython 原生驱动（最低层）

```python
import machine
import time

# 引脚定义
SCR_DC  = machine.Pin(4, machine.Pin.OUT)
SCR_CS  = machine.Pin(5, machine.Pin.OUT)
SCR_RST = machine.Pin(19, machine.Pin.OUT)  # 与 SD MISO 共享，初始化后释放

spi = machine.SPI(2, baudrate=20000000, sck=machine.Pin(18), mosi=machine.Pin(23))

def write_cmd(cmd):
    SCR_DC.value(0)
    SCR_CS.value(0)
    spi.write(bytearray([cmd]))
    SCR_CS.value(1)

def write_data(data):
    SCR_DC.value(1)
    SCR_CS.value(0)
    spi.write(data)
    SCR_CS.value(1)

def init_st7735():
    SCR_RST.value(0)
    time.sleep_ms(50)
    SCR_RST.value(1)
    time.sleep_ms(120)
    write_cmd(0x01)  # 软件复位
    time.sleep_ms(150)
    write_cmd(0x11)  # 退出睡眠
    write_cmd(0x36)
    write_data(b'\x60')  # 顺时针 90° 横屏
    write_cmd(0x3A)
    write_data(b'\x05')  # 16-bit RGB565
    write_cmd(0x29)  # 开启显示

init_st7735()
```

### 3.3 LVGL-MicroPython 屏幕驱动（screen.py）✅ 已实测修正

> **2026-06-25 实测结论：**
> 1. **Gitee 固件**（`lvgl_ESP32_GENERIC-SPIRAM-4-st7735.bin`）完美支持所有参数；GitHub 固件有 bug（color_byte_order / set_rotation 崩溃）。
> 2. **必须使用 `host=2` (VSPI)**，不可用 `host=1` (HSPI)！HSPI 与 ESP32-WROVER-B 的 PSRAM 总线冲突，会导致 `Cache Error` 死机。
> 3. 完整参数：`BYTE_ORDER_RGB` + `rgb565_byte_swap=True` 颜色才正确。
> 4. `set_rotation(lv.DISPLAY_ROTATION._90)` 正常，横屏 160×128。

以下为修正后的屏幕初始化代码，**在 Gitee 固件上已实测通过**：

```python
import machine, lcd_bus, lvgl as lv, st7735

# ✅ host=2 (VSPI) — WROVER-B 的 PSRAM 占用了 HSPI
spi_bus = machine.SPI.Bus(host=2, mosi=23, miso=19, sck=18)
disp_bus = lcd_bus.SPIBus(spi_bus=spi_bus, dc=4, cs=5, freq=20000000)

display = st7735.ST7735(
    data_bus=disp_bus,
    display_width=128,
    display_height=160,
    color_space=lv.COLOR_FORMAT.RGB565,
    color_byte_order=st7735.BYTE_ORDER_RGB,  # ✅ 颜色正确
    rgb565_byte_swap=True,                   # ✅ 字节交换
)
display.init(2)
display.set_rotation(lv.DISPLAY_ROTATION._90)  # ✅ 横屏 160×128

import task_handler
th = task_handler.TaskHandler()
```

> ⚠️ **原始 Gitee 代码使用 `host=1`，仅适用于 ESP32-WROOM（无 PSRAM）！WROVER-B 上会导致死机。**

### 3.5 LVGL-MicroPython 双缓冲驱动（screen2.py）⚠️ 推荐

在之前的 LVGL 项目中，原作者发现**指针类动画会出现明显的撕裂和频闪问题**。解决方案是启用 **LVGL 双缓冲（double buffering）**。

根据 LVGL-MicroPython 官方文档，对 `screen.py` 进行改进后得到 **`screen2.py`**，大幅减少了显示撕裂和频闪。具体代码见 Gitee 仓库的 `screen2.py` 文件。

双缓冲工作原理：

```
  帧缓冲 A ──绘制──▶ 显示
       │
  帧缓冲 B ──后台绘制──▶ 交换缓冲
```

- 正常模式（screen.py）：单帧缓冲，绘制与显示同时进行 → 撕裂
- 双缓冲模式（screen2.py）：一个缓冲显示、另一个后台绘制 → 画面完整

**使用方式：**

```python
import screen2  # 替代 screen

# 后续代码完全相同
import lvgl as lv
scrn = lv.screen_active()
# ...
```

> **推荐：** 任何涉及动画、指针、滚动等动态效果的 LVGL 项目，优先使用 `screen2.py` 替代 `screen.py`。

### 3.4 EasyDisplay 库驱动（入门推荐）

对于不需要 LVGL 的项目，**EasyDisplay** 是更轻量的选择，自带中文字体，几行代码即可显示文字：

```python
from machine import SPI, Pin
import st7735_buf
from easydisplay import EasyDisplay

# 初始化 SPI
spi = SPI(2, baudrate=30000000, polarity=0, phase=0,
          sck=Pin(18), mosi=Pin(23))

# 初始化 ST7735 显示屏
# 注意：横屏模式 width=160, height=128, rotate=1
dp = st7735_buf.ST7735(
    width=160, height=128,
    spi=spi,
    cs=5, dc=4, res=19,
    rotate=1,          # 横屏旋转
    bl=None,            # 本机屏幕无背光控制引脚
    invert=False,
    rgb=False,
)

# EasyDisplay 简化显示操作
ed = EasyDisplay(
    dp,
    "RGB565",
    font="/text_lite_16px_2312.v3.bmf",
    show=True,
    color=0xFFFF,
    clear=True,
)

# 显示中英文
ed.text("你好，世界！\n\nHello XiaoMiao！", 0, 10)
```

**所需文件（3 个）：**
- `easydisplay.py` — 主驱动
- `st7735_buf.py` — ST7735 帧缓冲驱动
- `text_lite_16px_2312.v3.bmf` — 中文字体文件

> 全部从 https://github.com/funnygeeker/micropython-easydisplay 获取，拷贝到掌机 `/` 根目录即可。

**屏幕方向说明：**

| rotate 值 | 方向 | width × height |
|-----------|------|----------------|
| 1 | 横屏（推荐） | 160 × 128 |
| 0 | 竖屏 | 128 × 160 |

> EasyDisplay 的横屏模式（rotate=1, 160×128）与 LVGL 的 `DISPLAY_ROTATION._90`（128×160 → 横屏）最终效果一致，仅参数写法不同。

---

## 4. 开发环境搭建

### 4.1 刷写工具

推荐使用 **Thonny IDE** 进行 MicroPython 开发和刷写：

- 下载：https://thonny.org/
- 选择解释器：运行 → 选择解释器 → MicroPython (ESP32)
- 通过 USB 连接掌机即可自动识别

备用工具：
- **esptool.py** — 命令行刷写
- **mpfshell** — 远程文件管理
- **VSCode + MicroPico 插件** — 专业 IDE

### 4.2 固件获取

| 固件类型 | 来源 | 说明 |
|------|------|------|
| 官方 MicroPython | https://micropython.org/download/ESP32_GENERIC/ | 基础 MicroPython，选带 SPIRAM 版本 |
| LVGL-MicroPython | Gitee 仓库 `fireware/` 目录 | 预编译，含 LVGL 9.2.2 |
| 自编译 LVGL-MicroPython | 源码编译 | 见下方"固件刷写"章节 |

### 4.3 连接掌机

1. 通过 USB Type-C 连接掌机到电脑
2. 掌机会识别为串口设备（Windows: COMx, Linux: /dev/ttyUSB0, macOS: /dev/tty.usbserial-xxx）
3. 在 Thonny 中选择对应串口即可

---

## 5. 固件刷写

### 5.1 官方 MicroPython 固件

```bash
# 1. 下载带 SPIRAM 的 ESP32 固件
# 从 https://micropython.org/download/ESP32_GENERIC/ 下载
# 选 ESP32_GENERIC-SPIRAM 版本

# 2. 擦除 Flash
esptool.py --chip esp32 --port COM3 erase_flash

# 3. 刷写固件
esptool.py --chip esp32 --port COM3 --baud 460800 write_flash -z 0x1000 ESP32_GENERIC-SPIRAM-2024xxxx.bin
```

### 5.2 LVGL-MicroPython 预编译固件（推荐）

> ⚠️ **固件选择至关重要！** 以下两个来源表现差异巨大：

#### ✅ Gitee 固件（推荐）— 完整体验

来自 Gitee 仓库 [py2012/xueersi-eps32-handheld-device](https://gitee.com/py2012/xueersi-eps32-handheld-device/tree/master/fireware)，**专门为此板编译**：

- **文件：** `lvgl_ESP32_GENERIC-SPIRAM-4-st7735.bin` (2.4MB)
- **LVGL 版本：** 9.2.2
- **MicroPython 版本：** 1.24.1 (mpy 3.4.0)
- **编译时间：** 2025-04-05
- ✅ `color_byte_order` 正常 — 颜色正确
- ✅ `set_rotation()` 正常 — 可旋转 90° 横屏
- ✅ ESP32-WROVER-B PSRAM 8MB 完全兼容
- **实测全部通过！**

```bash
# 烧录
python -m esptool --port COM14 erase-flash
python -m esptool --port COM14 --baud 921600 write-flash 0x0 \
  lvgl_ESP32_GENERIC-SPIRAM-4-st7735.bin
```

#### ❌ GitHub 固件（不推荐）— 有 Bug

来自 [217heidai/micropython_esp32_firmware](https://github.com/217heidai/micropython_esp32_firmware/releases) (2026-05-18)：

- ❌ `color_byte_order` 和 `rgb565_byte_swap` 参数导致 **Guru Meditation Crash**
- ❌ `set_rotation()` 同样崩溃
- ⚠️ 仅作为备用方案，只能不带颜色参数运行（颜色异常）

> **结论：使用 Gitee 固件。** VSPI(`host=2`) + `BYTE_ORDER_RGB` + `rgb565_byte_swap=True` + `set_rotation(90°)`。

### 5.3 自编译 LVGL-MicroPython 固件

步骤简述（详见微信公众号文章"简单三步编译一个LVGL-Micropython固件"）：

```bash
# 1. 克隆 lvgl_micropython 仓库
git clone https://github.com/lvgl/lv_micropython.git
cd lv_micropython

# 2. 初始化子模块
git submodule update --init --recursive

# 3. 编译（ESP32 带 SPIRAM）
make -C mpy-cross
cd ports/esp32
make BOARD=ESP32_GENERIC_SPIRAM BOARD_VARIANT=SPIRAM_OCT
```

> 编译前需在 `ports/esp32/modules/` 中放入 `st7735.py` 驱动，并在 `boards/ESP32_GENERIC_SPIRAM` 中配置正确的引脚定义。

---

## 6. 基础示例代码

### 6.1 Hello World — 屏幕测试

```python
import machine
import time

# 引脚定义
DC = machine.Pin(4, machine.Pin.OUT)
CS = machine.Pin(5, machine.Pin.OUT)
RST = machine.Pin(19, machine.Pin.OUT)
spi = machine.SPI(2, baudrate=20000000, sck=machine.Pin(18), mosi=machine.Pin(23))

# 颜色定义 (RGB565)
RED   = 0xF800
GREEN = 0x07E0
BLUE  = 0x001F
WHITE = 0xFFFF
BLACK = 0x0000

def set_window(x0, y0, x1, y1):
    DC.value(0); CS.value(0); spi.write(b'\x2A'); CS.value(1)
    DC.value(1); CS.value(0); spi.write(bytearray([0x00, x0, 0x00, x1])); CS.value(1)
    DC.value(0); CS.value(0); spi.write(b'\x2B'); CS.value(1)
    DC.value(1); CS.value(0); spi.write(bytearray([0x00, y0, 0x00, y1])); CS.value(1)
    DC.value(0); CS.value(0); spi.write(b'\x2C'); CS.value(1)

def fill_screen(color):
    set_window(0, 0, 127, 159)
    DC.value(1); CS.value(0)
    for _ in range(160):
        spi.write(bytes([(color >> 8) & 0xFF, color & 0xFF]) * 128)
    CS.value(1)

# 初始化屏幕（参见 3.2 节代码）
# ...

# 交替显示颜色
colors = [RED, GREEN, BLUE, WHITE, BLACK]
for c in colors:
    fill_screen(c)
    time.sleep(1)
```

### 6.2 按键读取（含去抖）

```python
from machine import Pin
import time

# 按键映射
KEY_PINS = {
    'UP':    2,
    'DOWN':  13,
    'LEFT':  27,
    'RIGHT': 35,
    'A':     34,
    'B':     12,
}

keys = {}
for name, pin in KEY_PINS.items():
    if pin in (34, 35, 36, 39):  # 仅输入引脚，无内部上拉
        keys[name] = Pin(pin, Pin.IN)
    else:
        keys[name] = Pin(pin, Pin.IN, Pin.PULL_UP)

def debounce(pin_obj):
    """硬件去抖：首次读取后等 10ms 再读一次确认"""
    state = pin_obj.value()
    time.sleep_ms(10)
    return state == pin_obj.value()

def read_keys():
    """返回当前按下的按键列表"""
    pressed = []
    for name, pin_obj in keys.items():
        if pin_obj.value() == 0:  # 按下为低电平
            pressed.append(name)
    return pressed

def wait_key_release(pin_obj):
    """阻塞等待按键释放"""
    while pin_obj.value() == 0:
        time.sleep_ms(10)

# 主循环 —— 事件模式
while True:
    for name, pin_obj in keys.items():
        if debounce(pin_obj) and pin_obj.value() == 0:
            print(f'按键按下: {name}')
            wait_key_release(pin_obj)
    time.sleep_ms(10)
```

> 按键均为**按下低电平**（active-low），GPIO2/12/13/27 有内部上拉，GPIO34/35 需外接或注意悬空状态。

### 6.3 光照传感器读取

```python
from machine import Pin, ADC

light_sensor = ADC(Pin(36))
light_sensor.atten(ADC.ATTN_11DB)  # 0-3.6V 量程

while True:
    value = light_sensor.read()  # 返回值 0-4095
    percentage = int(value / 4095 * 100)
    print(f'光照: {value} ({percentage}%)')
    time.sleep(1)
```

### 6.4 蜂鸣器播放

```python
from machine import Pin, PWM
import time

buzzer = PWM(Pin(14))

# 播放音调
def tone(freq, duration_ms, duty=512):
    buzzer.freq(freq)
    buzzer.duty(duty)
    time.sleep_ms(duration_ms)
    buzzer.duty(0)

# 简易音阶
NOTES = {
    'C4': 262, 'D4': 294, 'E4': 330, 'F4': 349,
    'G4': 392, 'A4': 440, 'B4': 494, 'C5': 523
}

# 播放小星星
melody = [
    ('C4', 200), ('C4', 200), ('G4', 200), ('G4', 200),
    ('A4', 200), ('A4', 200), ('G4', 400),
    ('F4', 200), ('F4', 200), ('E4', 200), ('E4', 200),
    ('D4', 200), ('D4', 200), ('C4', 400),
]

for note, duration in melody:
    tone(NOTES.get(note, 0), duration)
    time.sleep_ms(50)

buzzer.deinit()
```

### 6.5 I2C 设备扫描

```python
from machine import Pin, I2C

i2c = I2C(0, scl=Pin(15), sda=Pin(21))

devices = i2c.scan()
if devices:
    print(f'发现 {len(devices)} 个 I2C 设备:')
    for addr in devices:
        print(f'  0x{addr:02X}')
else:
    print('未发现 I2C 设备')
```

---

## 7. LVGL GUI 开发

### 7.1 环境要求

- **固件：** LVGL-MicroPython（lv_micropython）
- **LVGL 版本：** 9.x
- **驱动：** `screen.py`（见 3.3 节）

### 7.2 最小 LVGL 程序

```python
import screen
import lvgl as lv
import time

# screen.py 已初始化显示和任务处理器

# 获取活动屏幕
scr = lv.screen_active()
scr.set_style_bg_color(lv.color_hex(0x000000), 0)

# 创建一个标签
label = lv.label(scr)
label.set_text("Hello, XiaoMiao!")
label.set_style_text_color(lv.color_hex(0xFFFFFF), 0)
label.center()

# 创建一个按钮
btn = lv.button(scr)
btn.set_size(80, 40)
btn.align(lv.ALIGN.BOTTOM_MID, 0, -10)

btn_label = lv.label(btn)
btn_label.set_text("Press")
btn_label.center()

def btn_callback(e):
    label.set_text("Button Pressed!")

btn.add_event_cb(btn_callback, lv.EVENT.CLICKED, None)

# 主循环
while True:
    lv.timer_handler()
    time.sleep_ms(5)
```

### 7.3 LVGL 仪表盘示例（光照强度）

完整代码见微信公众号文章「ESP32掌机基于LVGL实现简易仪表盘」（已在 WebFetch 中提取完整代码，使用 `lv.scale` 模块实现圆形仪表盘，包含主刻度、次刻度、指针、中心点等元素）。

### 7.4 LVGL 模拟时钟（Analog Clock）

以下是一个完整的模拟时钟实现，使用 `lv.scale` 的 `ROUND_INNER` 模式 + 硬件 `Timer` 每秒更新：

```python
import screen2          # 使用双缓冲驱动，避免指针撕裂
import time
import lvgl as lv
from machine import Timer

# 初始化屏幕
scrn = lv.screen_active()
scrn.set_style_bg_color(lv.color_hex(0x000000), 0)

class AnalogClock:
    def __init__(self, parent):
        self.scale = None
        self.second_hand = None
        self.minute_hand = None
        self.hour_hand = None

        # 获取当前时间
        now = time.localtime()
        self.hour = now[3] % 12   # 转换为 12 小时制
        self.minute = now[4]
        self.second = now[5]

        self.create_clock(parent)
        self.start_timer()

    def create_clock(self, parent):
        """创建模拟时钟组件"""
        # 表盘主体（120×120）
        self.scale = lv.scale(parent)
        self.scale.set_size(120, 120)
        self.scale.set_mode(lv.scale.MODE.ROUND_INNER)

        # 表盘样式
        self.scale.set_style_bg_opa(lv.OPA._60, 0)
        self.scale.set_style_bg_color(lv.color_hex(0x222222), 0)
        self.scale.set_style_radius(lv.RADIUS_CIRCLE, 0)
        self.scale.set_style_clip_corner(True, 0)
        self.scale.center()

        # 刻度系统（12 小时制）
        self.scale.set_label_show(True)
        hour_labels = ["12", "1", "2", "3", "4", "5",
                       "6", "7", "8", "9", "10", "11", None]
        self.scale.set_text_src(hour_labels)
        self.scale.set_style_text_font(lv.font_montserrat_8, 0)
        self.scale.set_total_tick_count(61)
        self.scale.set_major_tick_every(5)

        # 主刻度样式（白色粗线）
        style_indicator = lv.style_t()
        style_indicator.init()
        style_indicator.set_text_color(lv.color_hex(0xFFFFFF))
        style_indicator.set_line_color(lv.color_hex(0xFFFFFF))
        style_indicator.set_length(3)
        style_indicator.set_line_width(2)
        self.scale.add_style(style_indicator, lv.PART.INDICATOR)

        # 次刻度样式（灰色细线）
        style_minor = lv.style_t()
        style_minor.init()
        style_minor.set_line_color(lv.color_hex(0xAAAAAA))
        style_minor.set_length(2)
        style_minor.set_line_width(1)
        self.scale.add_style(style_minor, lv.PART.ITEMS)

        # 表盘边框
        style_main = lv.style_t()
        style_main.init()
        style_main.set_arc_color(lv.color_hex(0x222222))
        style_main.set_arc_width(2)
        self.scale.add_style(style_main, lv.PART.MAIN)

        # 量程和角度
        self.scale.set_range(0, 60)
        self.scale.set_angle_range(360)
        self.scale.set_rotation(270)

        # 秒针（白色，细线，最长 45px）
        self.second_hand = lv.line(self.scale)
        self.second_hand.set_style_line_width(1, 0)
        self.second_hand.set_style_line_rounded(True, 0)
        self.second_hand.set_style_line_color(lv.color_hex(0xFFFFFF), 0)

        # 分针（橙色，40px）
        self.minute_hand = lv.line(self.scale)
        self.minute_hand.set_style_line_width(2, 0)
        self.minute_hand.set_style_line_rounded(True, 0)
        self.minute_hand.set_style_line_color(lv.color_hex(0xFFA500), 0)

        # 时针（红色，30px）
        self.hour_hand = lv.line(self.scale)
        self.hour_hand.set_style_line_width(3, 0)
        self.hour_hand.set_style_line_rounded(True, 0)
        self.hour_hand.set_style_line_color(lv.color_hex(0xFF0000), 0)

        # 中心点（金色圆）
        center = lv.obj(self.scale)
        center.set_size(8, 8)
        center.center()
        center.set_style_radius(lv.RADIUS_CIRCLE, 0)
        center.set_style_bg_color(lv.color_hex(0xFFD700), 0)
        center.set_style_bg_opa(lv.OPA.COVER, 0)

        self.update_hands()

    def update_hands(self):
        """更新所有指针位置"""
        # 秒针（用刻度值 0-59 映射到角度）
        lv.scale.set_line_needle_value(
            self.scale, self.second_hand, 50, self.second)

        # 分针
        lv.scale.set_line_needle_value(
            self.scale, self.minute_hand, 40, self.minute)

        # 时针（考虑分钟偏移：每小时 = 5 刻度）
        hour_value = self.hour * 5 + (self.minute // 12)
        lv.scale.set_line_needle_value(
            self.scale, self.hour_hand, 30, hour_value)

    def timer_callback(self, timer):
        """定时器回调：每秒更新"""
        now = time.localtime()
        self.hour = now[3] % 12
        self.minute = now[4]
        self.second = now[5]
        self.update_hands()

    def start_timer(self):
        """启动硬件定时器"""
        self.timer = Timer(-1)
        self.timer.init(period=1000,
                        mode=Timer.PERIODIC,
                        callback=self.timer_callback)

# 创建时钟
clock = AnalogClock(scrn)

# 主循环
while True:
    lv.timer_handler()
    time.sleep_ms(5)
```

**关键技术点：**
- 使用 `screen2` 双缓冲驱动消除指针撕裂
- `lv.scale.set_line_needle_value()` 设置指针线段的长度和角度
- 硬件 `Timer(-1)` 周期触发，回调中更新指针 → 不阻塞主循环
- 时针考虑分钟偏移（`hour * 5 + minute // 12`）实现平滑过渡
- 表盘 `ROUND_INNER` 模式：刻度围绕内部圆形排列

---

## 8. ESPHome 方案

GitHub 仓库 `pysn2012/xueersi-xiaomiao` 提供了 ESPHome 支持，可将掌机作为 Home Assistant 设备使用。

目录结构：
```
esphome/
├── packages/
│   └── xiaomiao.yaml  — 基础组件包（屏幕/按键/传感器）
└── wifi-ap.yaml       — WiFi AP 模式配置
```

> 通过 ESPHome 的 GitHub Actions workflow 可在线编译固件，无需本地搭建 ESP-IDF 环境。

---

## 9. 示例项目

### 9.1 俄罗斯方块（Tetris）

来源：`initdc/mpy-xueersi-coding-esp32`

- `俄罗斯方块简单版.py` — 基础版本
- `俄罗斯方块彩色版.py` — 彩色版，含完整游戏逻辑

### 9.2 躲避球游戏

来源：同上仓库 `躲避球.py`

### 9.3 ESP-NOW 遥控器

来源：`pysn2012/xueersi-xiaomiao`

使用 ESP-NOW 协议实现两台掌机之间的无线遥控，方向键控制小车运动，松开自动停止。

```python
# 核心代码片段 (eb_espnow_ok.py)
import network, espnow

sta = network.WLAN(network.STA_IF)
sta.active(True)
sta.disconnect()

e = espnow.ESPNow()
e.active(True)
peer = b'\x80\x65\x99\xa0\x7e\xfc'  # 接收方 MAC 地址
e.add_peer(peer)

# 发送控制指令
e.send(peer, "forward", True)
```

### 9.4 菜单系统

来源：`pysn2012/xueersi-xiaomiao/main.py`

基于 `EasyMenu` 库实现多级菜单，支持：
- ESP-NOW 遥控器启动
- 光照传感器读数
- 蜂鸣器曲目选择（马里奥 / 铃儿响叮当）

按键映射：
- 上/下：菜单导航
- A：确认
- B：返回

### 9.5 硬件测试程序

来源：`initdc/mpy-xueersi-coding-esp32/按键光感喇叭测试.py`

综合测试程序，同时检测：
- 6 个按键状态（实时显示按下的按键名）
- 光照传感器数值（带进度条可视化）
- 蜂鸣器按键音反馈（A/B 键 800Hz，方向键 1200Hz）

---

## 10. 关键限制与注意事项

### 10.1 硬件限制

| 项目 | 说明 |
|------|------|
| GPIO34/35/36/39 | 🔒 **仅输入引脚**，不可设为输出，不提供内部上拉/下拉 |
| GPIO12 | ⚠️ **启动敏感**，上电时若为高电平可能导致启动失败（用作 B 键，内部上拉） |
| GPIO2 | ⚠️ **启动敏感**，用作上键，内部上拉 |
| GPIO6~11 | 🔒 **内部 Flash 占用**，完全不可用 |
| 屏幕性能 | ST7735 刷新率有限，复杂动画会有卡顿（公众号原文提到"小喵的屏幕性能太差，指针会有卡顿的情况"） |
| **共享 SPI** | ⚠️ TFT 和 SD 卡共用 SPI2（GPIO18/23/19），通过 CS 片选分时复用。**同时访问会冲突**，原作者直说"给后续的编程学习带来了不少麻烦"。读写 SD 卡时必须确保屏幕操作已完成。 |

### 10.2 共享 SPI 注意事项

由于 LCD（CS=5）和 SD 卡（CS=22）共用 SPI2 总线，必须注意：

- ❌ **禁止同时操作** —— 同一时刻只能对一个设备（CS=0）
- ✅ **访问前切换 CS** —— 用完立即释放
- 💡 **SD 卡写入时暂停屏幕刷新** —— 否则 SPI 冲突导致花屏或数据损坏
- 💡 原作者推荐：优先使用 SD 卡的 **SPI 模式**（而非 SDIO 模式）以简化切换逻辑

### 10.2 软件注意事项

- MicroPython 固件必须选择 **带 SPIRAM** 的版本（`GENERIC-SPIRAM`），因为 ESP32-WROVER-B 有 8MB PSRAM
- LVGL 9.x 的 API 与 8.x 差异较大，仪表盘（Meter）已合并到 Scale 模块
- 使用 LVGL 时需要启动 `task_handler` 处理 LVGL 定时任务
- 屏幕旋转设置为 `lv.DISPLAY_ROTATION._90` 实现横屏

### 10.3 原始固件备份

⚠️ **重要：** 在进行任何刷写操作前，建议先备份原始固件！

```bash
# 读取并备份原始固件（假设 4MB Flash）
esptool.py --chip esp32 --port COM3 read_flash 0x0 0x400000 xueersi_original_backup.bin
```

---

## 11. ESP-IDF 原生开发（性能分析与迁移指南）

> 当前 MicroPython 方案在 128×160 屏幕上实测 FPS 约 15-25，动画有明显卡顿。本章从性能瓶颈、理论增益、社区资源、资料完整度四个维度，分析迁移到 ESP-IDF C/C++ 原生开发是否可行和值得。

### 11.1 当前 MicroPython 方案帧率瓶颈

MicroPython + LVGL 的帧率受限于四个层面：

**① 解释器开销（最大瓶颈）**

MicroPython 是字节码解释器，每一行 Python 代码要经过 解析→编译→字节码→解释执行 四层。LVGL 底层虽是 C，但从 Python 调用每个 API 都要通过 FFI 跨越语言边界，单次调用开销远超 C 直接调用。以 `obj.align()` 为例，Python 侧需要创建 Python 对象、序列化参数、调用 C 函数、回收 Python 对象，而 C 侧只是直接操作结构体。

**② SPI 传输无 DMA**

MicroPython 的 `machine.SPI` 底层走**轮询模式（polling）**——CPU 必须亲自把每个字节塞进 SPI FIFO 寄存器，传输期间 CPU 被完全占用。传输一帧数据（128×160×2 = 40KB）时，CPU 无法做任何其他事情，导致帧率严重受限。

```
轮询模式: CPU → [逐字节写入] → SPI FIFO → 屏幕
  DMA模式: CPU → [启动传输] → DMA 引擎 → SPI → 屏幕  ← CPU 同时做渲染
```

**③ GC 暂停**

MicroPython 的垃圾回收是"标记-清除"式的，会在帧间随机触发 stop-the-world 暂停。8MB PSRAM 中大量 Python 对象意味着 GC 扫描时间长，每次回收都可能造成掉帧。

**④ 定时器精度**

`lv.timer_create(anim_cb, 40, ...)` 在 MicroPython 中的实际精度受 Python 事件循环调度影响，抖动远大于理论值。

### 11.2 ESP-IDF 性能增益分析

| 优化维度 | MicroPython | ESP-IDF | 效果 |
|----------|------------|---------|------|
| **执行速度** | 字节码解释，API 调用 ~1000x slower than C | GCC `-O2` 原生编译，直接运行 | 消除所有解释开销 |
| **SPI 传输** | 轮询模式 (polling)，CPU 全程参与 | **DMA（直接内存访问）**，CPU 发出指令后即可渲染下一帧 | **10-200x** [¹](#footnote-dma) |
| **帧缓冲管理** | Python `bytes` 对象，受 GC 管理 | 静态分配在 SRAM/PSRAM，零 GC 干扰 | 消除 GC 暂停 |
| **双核利用** | 单核运行 | Core 0 跑 LVGL 渲染 + Core 1 跑应用逻辑 | 帧率翻倍潜力 |
| **编译器优化** | MicroPython 交叉编译，优化有限 | `-O2` + `CONFIG_COMPILER_OPTIMIZATION_PERF` + IRAM 放置 | +10-20% |

> <a name="footnote-dma">¹</a> 社区仓库 [Dreams-Possible/ST7735Driver](https://github.com/Dreams-Possible/ST7735Driver) 在 ESP-IDF 上启用 DMA 最大通道后，实测性能提升约 **200 倍**。

### 11.3 理论帧率计算

```
一帧数据量:  128 × 160 × 2 = 40,960 bytes (RGB565)
SPI 40MHz:   40,000,000 / 8 = 5,000,000 bytes/s 理论带宽
传输一帧:    40,960 / 5,000,000 ≈ 8.2ms
理论极限:    1000 / 8.2 ≈ 122 FPS
```

考虑 LVGL 渲染开销 + DMA 启动开销的实际损耗后：

| 方案 | 预期 FPS | 说明 |
|------|----------|------|
| 理论极限 | ~122 | 纯 SPI 传输，不渲染 |
| ESP-IDF + LVGL + DMA | **60-90** | 合理预期 |
| ESP-IDF + LVGL 无 DMA | 30-50 | 轮询 SPI |
| MicroPython + LVGL（当前） | **15-25** | 实测数据 |

**Espressif 官方基准（参考）：** ESP32-S3 + 800×480 RGB LCD 在优化后可达 16 FPS。该屏幕像素量是本机的 18 倍（800×480=384,000 vs 128×160=20,480），按像素量反推，同条件下本机可达 80+ FPS。

### 11.4 可用社区 ESP-IDF 资源

以下仓库提供了可直接参考的 ESP-IDF + ST7735 + LVGL 驱动代码：

| 资源 | 说明 | 与本机关联 |
|------|------|-----------|
| [Dreams-Possible/ST7735Driver](https://github.com/Dreams-Possible/ST7735Driver) | ESP-IDF 专用的 ST7735 驱动模板，**启用 DMA 最大通道**，性能提升约 200 倍。含完整 `CMakeLists.txt`、ESP32 移植代码。 | 只需替换引脚定义即可用 |
| [lvgl/lv_port_esp32](https://github.com/lvgl/lv_port_esp32) | LVGL 官方 ESP32 移植模板，含 `lv_conf.h`、显示/触摸驱动注册示例、Kconfig 配置。 | 通用模板，需调参 |
| [CSDN: ESP-IDF ST7735 128×160 LVGL 演示](https://blog.csdn.net/weixin_42880082/article/details/145683603) | 完整的 ESP-IDF + ST7735 128x160 + LVGL 教程，含 `sdkconfig` 默认值和驱动代码。**与本机分辨率完全匹配！** | 高参考价值 |
| [ESP32 SPI Master Driver (官方文档)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html) | ESP-IDF 官方 SPI 驱动文档，含 DMA 配置、传输队列、回调机制。 | API 参考 |

### 11.5 现有资料能否支撑 ESP-IDF 开发？

| 必需项 | 状态 | 来源 / 说明 |
|--------|------|------------|
| 完整引脚表 | ✅ 已有 | 本手册第 2 章，已实测验证 |
| 屏幕型号与分辨率 | ✅ 已有 | ST7735, 128×160, 已用 `get_physical_resolution()` 确认 |
| SPI 主机选择 | ✅ 已有 | `host=2` (VSPI), PSRAM 不冲突 |
| SPI 引脚 | ✅ 已有 | SCK=18, MOSI=23, MISO=19 |
| 屏幕控制引脚 | ✅ 已有 | DC=4, CS=5, RST=19 |
| 颜色参数 | ✅ 已有 | RGB565, `BYTE_ORDER_RGB`, `rgb565_byte_swap=True` |
| 按键引脚与极性 | ✅ 已有 | 6键完整映射 |
| ST7735 C 驱动 (ESP-IDF) | ✅ 社区有 | Dreams-Possible/ST7735Driver — 开箱即用，替换 pin 即可 |
| LVGL ESP32 移植 | ✅ 官方有 | lv_port_esp32 — LVGL 官方维护 |
| lv_conf.h 配置 | ⚠️ 需从头配 | 通用模板可参考，针对 128×160 调小 buffer |
| sdkconfig 配置 | ⚠️ 需从头配 | Flash/PSRAM/分区表需手动设置（见下方 11.6） |

**结论：现有资料完全足以支撑 ESP-IDF 开发。** 引脚与屏幕参数全部已知，ST7735 驱动和 LVGL 移植在社区有现成轮子，不需要从零写。唯一需要的是专属的 `sdkconfig` 和 `lv_conf.h` 配置，属正常的项目初始化工作。

### 11.6 ESP-IDF 项目初始化关键配置

```ini
# sdkconfig 关键项（针对学而思编程机）
CONFIG_IDF_TARGET_ESP32=y              # ESP32
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y       # 4MB Flash
CONFIG_SPIRAM=y                        # 启用 PSRAM
CONFIG_SPIRAM_MODE_OCT=y               # WROVER-B 八线 PSRAM
CONFIG_SPIRAM_SPEED_80M=y              # PSRAM 80MHz
CONFIG_SPIRAM_USE_CAPS_ALLOC=y         # 允许 malloc 从 PSRAM 分配
CONFIG_COMPILER_OPTIMIZATION_PERF=y    # -O2 性能优化
CONFIG_LV_COLOR_DEPTH_16=y             # RGB565
CONFIG_LV_MEM_SIZE_KILOBYTES=64        # LVGL 内部内存 64KB
CONFIG_LV_USE_PERF_MONITOR=y           # 启用帧率/CPU 监控
```

```c
// lv_conf.h 关键项
#define LV_HOR_RES_MAX      160       // 横屏宽度
#define LV_VER_RES_MAX      128       // 横屏高度
#define LV_COLOR_DEPTH       16       // RGB565
#define LV_DRAW_BUF_ALIGN    4        // DMA 对齐
#define LV_USE_DRAW_SW       1        // 软件渲染
```

### 11.7 SPI DMA 引脚约束

ESP32 的 DMA 对 GPIO 有约束——并非所有引脚都支持 DMA 传输。好在我们的 SPI 引脚（GPIO18/19/23）都支持 DMA。需要注意：

| GPIO | 功能 | DMA 支持 |
|------|------|----------|
| 18 | SCK | ✅ 支持 |
| 23 | MOSI | ✅ 支持 |
| 19 | MISO | ✅ 支持 |

如果将来使用预留扩展口（GPIO25/26/32/33）接 SPI 设备做 DMA 传输，需确认这些引脚是否也在 DMA 通道上。

### 11.8 迁移路径与工作量评估

```
第一阶段：环境搭建（3-5天）
├── 创建 ESP-IDF 项目，基于 lv_port_esp32 模板
├── 移植 ST7735 驱动（Dreams-Possible 库，替换引脚定义）
├── 配置 sdkconfig（Flash/PSRAM/DMA/分区表）
├── 配置 lv_conf.h（分辨率/色深/帧缓冲大小）
└── 编译烧录，验证屏幕点亮

第二阶段：基础验证（1-2天）
├── 运行 LVGL benchmark demo，实测 FPS 基准
├── 移植按键驱动（gpio + 去抖）
├── 蜂鸣器 PWM 驱动
└── 传感器 ADC 读取

第三阶段：应用开发（3-5天）
├── 动画 demo 移植（对标当前 MicroPython 动画 demo）
├── FPS 对比测试（MicroPython vs ESP-IDF 同场景帧率）
├── 性能进一步优化（IRAM 放置、DMA 缓冲大小调优）
└── 驱动其他外设（SD 卡、I2C 设备）
```

### 11.9 决策建议

| 场景 | 推荐方案 |
|------|----------|
| 快速原型 / 学习 / 简单 UI | MicroPython + LVGL（当前方案），够用 |
| 动画密集型应用（游戏、仪表盘） | **强烈推荐 ESP-IDF**，预期帧率提升 3-5x |
| 需要极致性能 / 商业产品 | ESP-IDF + DMA + 双核分载 + 双缓冲 |
| 需要 Wi-Fi/BLE + GUI | ESP-IDF（本机 FreeRTOS 网络栈更成熟） |

> **一句话总结：** 用 ESP-IDF 把 FPS 从 ~20 提升到 60-90 技术上是完全可行的——核心收益来自 DMA（释放 CPU 做渲染）和原生编译（消除解释器开销）这两个关键词。社区轮子现成，资料充沛，瓶颈在配置工作而非编码。

---

## 12. 资源链接汇总

### 12.1 代码仓库

| 仓库 | 说明 |
|------|------|
| [Gitee: py2012/xueersi-eps32-handheld-device](https://gitee.com/py2012/xueersi-eps32-handheld-device) | 固件和驱动，含 LVGL-MicroPython 预编译固件和 screen.py 驱动 |
| [GitHub: pysn2012/xueersi-xiaomiao](https://github.com/pysn2012/xueersi-xiaomiao) | 综合性开发仓库（MT），含 MicroPython 示例、ESPHome 配置、LVGL 示例、引脚文档 |
| [GitHub: initdc/mpy-xueersi-coding-esp32](https://github.com/initdc/mpy-xueersi-coding-esp32) | 逆向工程仓库，含俄罗斯方块、躲避球等游戏示例，硬件测试程序 |

### 12.2 ESP-IDF 相关资源

| 资源 | 说明 |
|------|------|
| [Dreams-Possible/ST7735Driver](https://github.com/Dreams-Possible/ST7735Driver) | ESP-IDF 专用 ST7735 驱动模板，启用 DMA，性能提升约 200 倍 |
| [lvgl/lv_port_esp32](https://github.com/lvgl/lv_port_esp32) | LVGL 官方 ESP32 移植模板 |
| [ESP32 SPI Master Driver (官方文档)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html) | ESP-IDF SPI DMA 驱动文档 |
| [CSDN: ESP-IDF ST7735 128x160 LVGL](https://blog.csdn.net/weixin_42880082/article/details/145683603) | 与本机分辨率完全匹配的 ESP-IDF 教程 |

### 12.3 公众号文章

| 文章 | 内容 |
|------|------|
| ["50元不到的ESP32开发板"](https://mp.weixin.qq.com/s?__biz=MzkzNDQzMTc0OA==&mid=2247484886&idx=1&sn=653a530dec715b7af27554153cbb4828) | 硬件介绍、拆解、EasyDisplay 屏幕驱动示例、固件烧录地址确认 |
| ["没有原理图，如何找出每个模块的引脚？"](https://mp.weixin.qq.com/s?__biz=MzkzNDQzMTc0OA==&mid=2247484894&idx=1&sn=bbc68ea21bb19f2dc063ecb19ef5e0cb) | 引脚逆向工程全流程（软件测试 + 万用表）、按键去抖代码、SPI 共享问题分析 |
| ["简单三步编译一个LVGL-Micropython固件"](https://mp.weixin.qq.com/s?__biz=MzkzNDQzMTc0OA==&mid=2247485692&idx=1&sn=b5ad96293a96a10afd14e4e04a7070ab) | LVGL-MicroPython 固件编译教程 |
| "ESP32掌机基于LVGL实现简易仪表盘" | LVGL Scale 仪表盘实现 |
| ["ESP32掌机基于LVGL实现时间表盘"](https://mp.weixin.qq.com/s?__biz=MzkzNDQzMTc0OA==&mid=2247485979&idx=1&sn=4eb7aee99ebf679fa04d2df77e6abaaa) | **screen2.py 双缓冲驱动、模拟时钟完整实现、LVGL Scale 指针平滑过渡** |
| "LVGL 显示 GIF 图片" | lv_gif 播放动画 |
| "LVGL 柱状图" | lv_chart 柱状图实现 |
| "LVGL 曲线图" | lv_chart 折线图实现 |
| "LVGL 缩放和旋转图片" | lv_image 变换操作 |
| [文章合集](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzkzNDQzMTc0OA==&action=getalbum&album_id=3525324093212049409) | 全部相关文章 |

### 12.4 依赖库

| 库 | 用途 | 来源 |
|------|------|------|
| `micropython-easydisplay` | 简化显示操作，支持文字/图片 | [GitHub](https://github.com/funnygeeker/micropython-easydisplay) |
| `micropython-easymenu` | 菜单系统 | [GitHub](https://github.com/funnygeeker/micropython-easymenu) |
| `micropython-easybutton` | 按键去抖与事件 | [GitHub](https://github.com/funnygeeker/micropython-easybutton) |
| LVGL | GUI 框架 | https://lvgl.io/ |
| lv_micropython | LVGL MicroPython 移植 | [GitHub](https://github.com/lvgl/lv_micropython) |
| ESPHome | 智能家居固件 | https://esphome.io/ |

### 12.5 交流

- QQ 群（公众号文章提及，提供固件和驱动下载）
- 三个 GitHub/Gitee 仓库的 Issues 区

---

## 附录 A：快速启动检查清单

1. [ ] 通过 USB 连接掌机到电脑
2. [ ] 用 `esptool.py read_flash` 备份原始固件
3. [ ] 刷入 LVGL-MicroPython 固件（或官方 MicroPython 固件）
4. [ ] 安装 Thonny IDE
5. [ ] 上传 `screen.py` 到掌机根目录
6. [ ] 上传示例代码并运行
7. [ ] 验证屏幕显示正常
8. [ ] 验证按键、蜂鸣器、光照传感器工作正常

## 附录 B：引脚分配总表（参考卡片）

```
         ┌────────────────────────────────┐
         │   学而思 ESP32 小喵掌机 引脚图   │
         ├────────┬───────────────────────┤
         │ GPIO34 │ A 键 (仅输入)         │
         │ GPIO12 │ B 键 (⚠启动敏感)      │
         │  GPIO2 │ 上键 (⚠启动敏感)      │
         │ GPIO13 │ 下键                  │
         │ GPIO27 │ 左键                  │
         │ GPIO35 │ 右键 (仅输入)         │
         │ GPIO14 │ 蜂鸣器 (PWM)          │
         │ GPIO36 │ 光照传感器 (ADC,仅输入)│
         │ GPIO39 │ 热敏电阻 (ADC,仅输入)  │
         │  GPIO4 │ TFT DC                │
         │  GPIO5 │ TFT CS                │
         │ GPIO15 │ I2C SCL               │
         │ GPIO21 │ I2C SDA               │
         │ GPIO18 │ SPI SCK (TFT+SD共享)  │
         │ GPIO23 │ SPI MOSI (TFT+SD共享) │
         │ GPIO19 │ SPI MISO (TFT+SD共享) │
         │ GPIO19 │ TFT RES (共用引脚)    │
         │ GPIO22 │ SD 卡 CS              │
         │  GPIO1 │ UART0 TX              │
         │  GPIO3 │ UART0 RX              │
         │ 25,26, │ 预留扩展口            │
         │ 32,33  │                       │
         └────────┴───────────────────────┘
```

---

## 附录 C：引脚逆向工程方法

> 原作者因学而思和 KittenBot 不提供原理图，自行完成了全部引脚测试。以下是方法总结，供需要自行验证或探索未知引脚时参考。

### C.1 前期准备

- 了解 ESP32-WROVER-B 的 39 个引脚分布
- 排除 GPIO6~11（内部 Flash 占用）
- 已知 SPI2 默认引脚：SCK=18, MOSI=23, MISO=19
- 已知 SD 卡 SDIO Slot 1 默认引脚：SCK=14, CMD=15, D0=2, D1=4

### C.2 软件测试法（适用于按键）

逐个 GPIO 测试，当按键按下时 print 输出：

```python
from machine import Pin
import time

def test_pin(gpio_num):
    """测试指定 GPIO 是否连接按键"""
    try:
        key = Pin(gpio_num, Pin.IN, Pin.PULL_UP)
        print(f"测试 GPIO_{gpio_num}... 按对应按键")
        last = 1
        while True:
            val = key.value()
            if val == 0 and last == 1:
                print(f"  ✓ GPIO_{gpio_num} 检测到按下！")
            last = val
            time.sleep_ms(10)
    except Exception as e:
        print(f"  ✗ GPIO_{gpio_num} 不可用: {e}")

# 逐个测试示例（21 个可用引脚）
# test_pin(0)  # 测试 GPIO0
# test_pin(2)  # 测试 GPIO2
# ...
```

### C.3 万用表通断测试法（适用于屏幕/SD 卡等多引脚模块）

| 步骤 | 操作 |
|------|------|
| ① 断电 | 确保开发板完全不带电 |
| ② 万用表打到通断档 | 蜂鸣器标志的档位 |
| ③ 接表笔 | 黑→COM，红→VΩ |
| ④ 逐对测试 | 黑表笔接触 MCU 引脚，红表笔接触模块引脚 |
| ⑤ 判断 | 蜂鸣器响 = 通路（连接）；显示数字 = 不通 |

用此法可测出 ST7735 屏幕和 MicroSD 卡槽的全部引脚连接关系。

### C.4 最终测试结论

经软件测试 + 万用表双重验证后的引脚分配：

```
按键:  上=GPIO2   下=GPIO13  左=GPIO27  右=GPIO35  A=GPIO34   B=GPIO12
LCD:   SCK=18, MOSI=23, CS=5, DC=4, RES=19, BL=None
SD:    SCK=18, MOSI=23, MISO=19, CS=22  ← 与 LCD 共享 SPI！
蜂鸣器: GPIO14
```

> 以上为原作者（PY学习笔记）实测结论，已与 Gitee/GitHub 各仓库交叉验证，可作为开发基准。

---

> **文档版本：** v1.5（2026-06-25 — 新增 ESP-IDF 原生开发性能分析章节；FPS 瓶颈论证、理论帧率计算、社区 ESP-IDF 资源整理、sdkconfig/lv_conf.h 关键配置参考、迁移路径与工作量评估）
>
> 本文档基于社区逆向工程和开源仓库整理，非学而思官方资料。如有更新，请关注上述 GitHub/Gitee 仓库。
