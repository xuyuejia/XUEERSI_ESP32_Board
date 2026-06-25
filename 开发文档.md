# XRS Coding Machine — LVGL 动画演示 开发文档

> 硬件：ESP32-WROVER-B + ST7735 128×160 LCD（横屏 160×128）  
> 框架：ESP-IDF v5.4 + LVGL v9（通过 `esp_lvgl_port` 组件集成）

---

## 一、硬件引脚映射

| 功能 | GPIO | 说明 |
|------|------|------|
| SPI MOSI | 23 | 数据输出 |
| SPI SCLK | 18 | 时钟 |
| SPI MISO | 19 | 数据输入（ST7735 不需要回读，但该引脚与 LCD RST 共用） |
| TFT DC | 4 | 数据/命令选择 |
| TFT CS | 5 | 片选 |
| TFT RST | -1 | 复位（本板未连接，靠软件复位） |
| TFT BL | -1 | 背光（未连接，常亮） |
| BTN UP | 2 | 上键（加速） |
| BTN DOWN | 13 | 下键（减速） |
| BTN LEFT | 27 | 左键（切换模式） |
| BTN RIGHT | 35 | 右键（切换模式） |
| BTN A | 34 | 预留 |
| BTN B | 12 | 预留 |

---

## 二、开发环境搭建

### 2.1 必备工具

- **ESP-IDF v5.4**：安装在 `C:\Users\ThinkPad\esp-idf-v5.4`
- **Python 3.12**：ESP-IDF 构建依赖
- **ESP-IDF Python 环境**：`C:\Users\ThinkPad\.espressif\python_env\idf5.4_py3.12_env`

### 2.2 构建命令

```powershell
# 在项目根目录执行
python build.py
```

`build.py` 自动设置 `IDF_PATH` 和 Python 环境变量后调用 `idf.py build`。

### 2.3 烧录命令

```powershell
# 方法一：使用 idf.py（需先设置环境变量）
idf.py -p COM14 flash monitor

# 方法二：使用 esptool 直接烧录（COM14 为串口号，按实际修改）
python -m esptool --chip esp32 -b 460800 -p COM14 ^
  --before default_reset --after hard_reset write_flash ^
  --flash_mode dio --flash_size 4MB --flash_freq 40m ^
  0x1000 build\bootloader\bootloader.bin ^
  0x8000 build\partition_table\partition-table.bin ^
  0x10000 build\xrs_lvgl_demo.bin
```

---

## 三、核心注意事项（踩坑总结）

### ⚠️ 3.1 LVGL 旋转配置必须与面板硬件状态一致

**这是最容易出错的配置！**

```c
// ===== display_init() 中设置的面板状态 =====
esp_lcd_panel_swap_xy(panel, true);   // 交换 XY → 横屏
esp_lcd_panel_mirror(panel, true, false);  // X 镜像

// ===== lvgl_init() 中的 rotation 必须完全匹配 =====
.rotation = {
    .swap_xy = true,   // ← 必须与上面 esp_lcd_panel_swap_xy 参数相同
    .mirror_x = true,  // ← 必须与上面 esp_lcd_panel_mirror 参数相同
    .mirror_y = false, // ← 必须与上面 esp_lcd_panel_mirror 参数相同
},
```

**原因**：`lvgl_port_add_disp()` 内部会调用 `lvgl_port_disp_rotation_update()`，根据 `rotation` 配置**直接写入**面板 MADCTL 寄存器。如果此处的值与面板实际状态不一致，面板会被改到错误的方向，导致 LVGL 按横屏渲染但面板以竖屏显示 → **花屏**。

### ⚠️ 3.2 `buffer_size` 单位是像素，不是字节

```c
// ❌ 错误：多乘了 sizeof(lv_color_t)，导致分配 2 倍大小的缓冲区
.buffer_size = SCR_HOR_RES * SCR_VER_RES * sizeof(lv_color_t),

// ✅ 正确：填像素数
.buffer_size = SCR_HOR_RES * SCR_VER_RES,  // 160 × 128 = 20480 像素
```

`esp_lvgl_port` 内部会根据 `color_format` 自动乘以每像素字节数来分配实际内存。

### ⚠️ 3.3 ESP32 上 PSRAM + DMA 存在缓存一致性问题

```c
// ❌ 同时开启会导致花屏（ESP32 的已知问题）
.flags = {
    .buff_dma = true,
    .buff_spiram = true,  // PSRAM 分配 + DMA 传输 → 缓存不一致
},

// ✅ 正确做法二选一：
// 方案 A：DMA + 内部 SRAM（推荐，性能好）
.flags = { .buff_dma = true, .buff_spiram = false },

// 方案 B：无 DMA + PSRAM（内存大，CPU 搬运稍慢）
.flags = { .buff_dma = false, .buff_spiram = true },
```

**原因**：ESP32 的 CPU 通过 data cache 访问 PSRAM。当 LVGL 渲染完成后，数据可能还在 cache 中未写回 PSRAM。此时 DMA 直接从 PSRAM 读取，拿到的是过期数据 → **花屏**。

**注意**：使用方案 A 需要确保内部 SRAM 有足够空间。本项目的双缓冲全屏缓冲区约 80KB（160×128×2 字节×2），ESP32 内部 ~520KB DRAM 足够。

### ⚠️ 3.4 ST7735 必须发送额外的初始化寄存器

ST7789 驱动的 `esp_lcd_panel_init()` 只发送 SLPOUT、MADCTL、COLMOD、RAMCTRL 四条指令。ST7735 还需要额外的 gamma 校正、电源管理和帧率控制寄存器才能正常显示：

| 寄存器 | 名称 | 说明 |
|--------|------|------|
| 0xB1–0xB3 | FRMCTR1–3 | 帧率控制 |
| 0xB4 | INVCTR | 反转控制（点反转） |
| 0xC0–0xC5 | PWCTR1–5 + VMCTR1 | 电源管理 |
| 0xE0 | Gamma (+) | 正极性 gamma 校正（16 字节） |
| 0xE1 | Gamma (-) | 负极性 gamma 校正（16 字节） |
| 0x13 | NORON | 正常显示模式 |

**注意**：不同批次的 ST7735 面板可能需要不同的 gamma 值。如果颜色偏差严重，检查 0xE0/0xE1 的值。

### ⚠️ 3.5 `swap_bytes` 的作用

```c
.flags = {
    .swap_bytes = true,  // 将 LVGL 输出的小端序 RGB565 转为大端序
},
```

ESP32 是小端序架构，LVGL 内部的 `lv_color16_t` 在内存中按小端序存储（低字节在前）。而 ST7735 默认为大端序接收（高字节在前）。`swap_bytes = true` 在 flush 回调中调用 `lv_draw_sw_rgb565_swap()` 交换每对字节，使数据格式匹配面板要求。

### ⚠️ 3.6 SPI 主机选择

```c
#define SPI_HOST_ID  SPI2_HOST  // VSPI，避免与 PSRAM 的 SPI0/1 冲突
```

ESP32 有 3 个 SPI 外设：
- **SPI0/1**：被 Flash 和 PSRAM 占用，不可用
- **SPI2 (VSPI)**：可用于外设 ✅
- **SPI3 (HSPI)**：可用于外设，但部分引脚与 JTAG 冲突

选择 SPI2_HOST 避免与 PSRAM 共用 SPI 总线。

### ⚠️ 3.7 MADCTL 寄存器（方向控制）

ST7735 的 MADCTL (0x36) 寄存器各位含义：

| 位 | 名称 | 功能 | 横屏值 |
|----|------|------|--------|
| 7 | MY | 行地址递增方向 | 0（从上到下） |
| 6 | MX | 列地址递增方向 | 1（从右到左） |
| 5 | MV | 行列交换 | 1（交换 XY） |
| 4 | ML | 垂直刷新方向 | 0（从上到下） |
| 3 | BGR | RGB/BGR 顺序 | 0（RGB） |

横屏 160×128（旋转 90°）对应 MADCTL = **0x60** = `MX|MV`。

---

## 四、项目结构

```
xrs_lvgl_demo/
├── main/
│   ├── main.c              # 主程序（显示初始化 + LVGL UI + 动画）
│   ├── CMakeLists.txt      # 主组件构建配置
│   └── idf_component.yml   # 依赖声明
├── components/
│   ├── esp_lvgl_port/      # ESP LVGL 移植层
│   └── lvgl/               # LVGL 图形库（v9）
├── build/                  # 构建输出
│   ├── xrs_lvgl_demo.elf   # ELF 可执行文件
│   ├── xrs_lvgl_demo.bin   # 烧录用的二进制文件
│   └── bootloader/         # bootloader
├── build.py                # 构建脚本
├── flash.py                # 烧录脚本
├── CMakeLists.txt          # 顶层 CMake
├── sdkconfig               # Kconfig 配置
└── sdkconfig.defaults      # 默认 Kconfig 值
```

---

## 五、LVGL 配置要点

```c
const lvgl_port_cfg_t lvgl_cfg = {
    .task_priority = 5,        // LVGL 任务优先级
    .task_stack = 8192,        // 任务栈大小（字节）
    .task_affinity = 1,        // 绑定到 Core 1，避免与 Core 0 的 app 冲突
    .task_max_sleep_ms = 500,  // 最大空闲睡眠时间
    .timer_period_ms = 5,      // LVGL 定时器精度
};
```

- **task_affinity = 1**：将 LVGL 渲染任务绑定到 Core 1，Core 0 处理主循环和按钮中断，避免相互阻塞。
- **双重缓冲**：`double_buffer = true` 配合 DMA，在一个缓冲区传输到 LCD 时，LVGL 可以同时渲染到另一个缓冲区。

---

## 六、常见问题排查

| 现象 | 可能原因 | 检查项 |
|------|----------|--------|
| 花屏/雪花噪点 | PSRAM DMA 缓存不一致 | 3.3 节 |
| 花屏（方向错乱） | LVGL rotation 与面板不匹配 | 3.1 节 |
| 颜色错误（红蓝互换） | `swap_bytes` 设反了 | 3.5 节 |
| 画面偏移 | `x_gap/y_gap` 未设置 | 调用 `esp_lcd_panel_set_gap()` |
| 画面不更新 | LVGL 任务未运行 | 检查 task_affinity 和优先级 |
| 编译失败 | ESP-IDF 环境未设置 | 2.2 节，运行 `build.py` |
| 烧录失败 / ELF 缺失 | `set-target` 触发了 fullclean | 去掉 `build.py` 中的 `set-target` 行 |
| SPI 通信失败 | 引脚配置错误或频率过高 | 3.6 节，尝试降低到 20MHz |
| 内存不足（启动即 crash） | 内部 SRAM 不够装双缓冲 | 改用 `buff_spiram=true, buff_dma=false` |

---

## 七、代码修改记录

| 日期 | 问题 | 修复 |
|------|------|------|
| 2026-06-25 | 花屏（主因） | `buff_spiram` 从 `true` 改为 `false`（PSRAM DMA 缓存一致性） |
| 2026-06-25 | 花屏（次因） | `rotation.swap_xy/mirror_x` 从 `false` 改为 `true`（与面板匹配） |
| 2026-06-25 | 浪费内存 | `buffer_size` 从 `*sizeof(lv_color_t)` 改为像素数 |
| 2026-06-25 | 每次构建触发全量重编 | 去掉 `build.py` 中的 `set-target` 步骤 |
