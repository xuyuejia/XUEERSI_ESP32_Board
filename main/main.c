/**
 * ================================================================
 * 学而思编程机（小喵掌机）— LVGL 动画演示程序
 * ================================================================
 *
 * 【硬件平台】
 *   - MCU:    ESP32-WROVER-B (双核 Xtensa LX6, 240MHz, 8MB PSRAM)
 *   - LCD:    ST7735S (物理 128×160 竖屏 → 旋转 90° 后 160×128 横屏)
 *   - 接口:   4 线 SPI (MOSI/SCLK/DC/CS), MISO 与 RST 共用 GPIO19
 *   - 输入:   6 个机械按键 (上下左右 + A/B), 低电平触发
 *
 * 【软件栈】
 *   - ESP-IDF v5.4 (底层驱动 / FreeRTOS / SPI / GPIO)
 *   - LVGL v9      (图形库, 通过 esp_lvgl_port 组件集成)
 *   - 构建: CMake + Ninja, 通过 build.py 脚本一键编译
 *
 * 【功能】
 *   - 实时 FPS 帧率显示 (绿色≥30 / 黄色≥20 / 红色<20)
 *   - 彩虹色动画方块 (三种模式: WAVE脉冲 / BOUNCE弹跳 / ROTATE旋转)
 *   - 环绕粒子动画
 *   - 双波形进度条
 *   - 方向键切换模式 / 上下键调节速度 1×–5×
 *
 * 【⚠️ 踩坑注意事项】
 *   1. buffer_size 单位是像素数，不是字节数 (esp_lvgl_port 内部会自动乘颜色深度)
 *   2. LVGL 的 rotation.swap_xy / mirror_x / mirror_y 必须与 display_init() 中
 *      面板的 swap_xy / mirror 设置完全一致，否则 LVGL 端口初始化时会覆盖面板方向
 *   3. ESP32 上不能同时开 buff_dma=true + buff_spiram=true——PSRAM 的 DMA 有
 *      CPU 缓存一致性问题，DMA 会读到过期数据导致花屏
 *   4. ST7735 需要额外的 Gamma/Power/FrameRate 初始化寄存器，ST7789 驱动不会发送
 *   5. 本板 LCD RST 和 SPI MISO 共用 GPIO19 (PIN_TFT_RST=-1 即不接复位线)
 *
 * 【坐标体系】横屏 160×128
 *   (0,0) ──────── (159,0)
 *    │    [FPS]      │
 *    │  ◉ 动画区     │
 *    │ [条1] [条2]  │
 *   (0,127) ────── (159,127)
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"        /* 多任务调度 */
#include "freertos/task.h"            /* 任务创建/延时 */
#include "freertos/semphr.h"          /* 信号量 */
#include "esp_log.h"                  /* 日志输出 (ESP_LOGI/ESP_LOGE) */
#include "esp_timer.h"                /* 高精度定时器 (用于 FPS 计算) */
#include "driver/spi_master.h"        /* SPI 总线驱动 */
#include "driver/gpio.h"             /* GPIO 输入/中断 */
#include "esp_lcd_panel_io.h"         /* LCD 面板 IO 抽象层 */
#include "esp_lcd_panel_io.h"         /* (重复 include 可移除, 保留无害) */
#include "esp_lcd_panel_ops.h"        /* LCD 面板操作 (swap_xy/mirror/on_off) */
#include "esp_lcd_panel_st7789.h"     /* ST7789 驱动 — 与 ST7735 指令兼容 */
#include "esp_lvgl_port.h"            /* ESP LVGL 移植层 */

static const char *TAG = "XRS_DEMO";

/* ================================================================
 * 引脚定义 (已在 学而思编程机/小喵掌机 验证)
 *
 * 【SPI 总线】
 *   - 使用 SPI2_HOST (VSPI), 避免与 PSRAM 的 SPI0/1 冲突
 *   - ESP32 有三个 SPI 外设: SPI0(Flash), SPI1(PSRAM), SPI2(VSPI), SPI3(HSPI)
 *   - GPIO19 在本板上同时连接 LCD_RST 和 SPI_MISO:
 *     作为 MISO 输入使用时不存在冲突, 但需注意配置
 * ================================================================ */
#define PIN_SPI_MOSI    23   /* SPI 主机输出 (Master Out Slave In) */
#define PIN_SPI_SCLK    18   /* SPI 时钟 */
#define PIN_SPI_MISO    19   /* SPI 主机输入 (本板与 LCD RST 共用, 但 ST7735 不回读数据) */
#define PIN_TFT_DC      4    /* 数据/命令选择 (Data/Command) */
#define PIN_TFT_CS      5    /* 片选 (Chip Select, 低有效) */
#define PIN_TFT_RST     -1   /* 复位 (本板未连接, 值为负时跳过硬件复位) */
#define PIN_TFT_BL      -1   /* 背光 (本板未连接, 背光常亮) */

/* 按键 GPIO — 内部上拉, 按下接 GND → 低电平触发 */
#define PIN_BTN_UP      2    /* 上键: 增加动画速度 (1× → 5×) */
#define PIN_BTN_DOWN    13   /* 下键: 降低动画速度 */
#define PIN_BTN_LEFT    27   /* 左键: 切换到上一个动画模式 */
#define PIN_BTN_RIGHT   35   /* 右键: 切换到下一个动画模式 */
#define PIN_BTN_A       34   /* A 键 (预留) */
#define PIN_BTN_B       12   /* B 键 (预留) */

/* ================================================================
 * 显示分辨率配置
 *
 * ST7735 物理分辨率为 128(W)×160(H) (竖屏/Portrait)
 * 通过 MADCTL 寄存器旋转 90° 后得到 160(W)×128(H) 横屏/Landscape
 *
 * SCR_HOR_RES / SCR_VER_RES 是 LVGL 看到的"逻辑"分辨率 (横屏)
 * ================================================================ */
#define DISP_WIDTH      128   /* 物理宽度 (竖屏短边) */
#define DISP_HEIGHT     160   /* 物理高度 (竖屏长边) */
#define SCR_HOR_RES     DISP_HEIGHT   /* 横屏 160 列 */
#define SCR_VER_RES     DISP_WIDTH    /* 横屏 128 行 */

/* ================================================================
 * SPI 配置
 *
 * SPI2_HOST = VSPI, 避免与 Flash/PSRAM 的 SPI0/SPI1 冲突
 * 40MHz 是 ST7735 和本板布线下验证可用的稳定频率
 * 如遇通信不稳定可降至 20MHz 排查
 * ================================================================ */
#define SPI_HOST_ID     SPI2_HOST
#define SPI_CLK_HZ      (40 * 1000 * 1000)

/* ================================================================
 * 全局状态
 *
 * anim_phase:   动画相位 [0, 1), 驱动所有基于时间的动画
 * anim_speed:   速度倍率 1–5, 影响 step 大小
 * bounce_x/dir: 弹跳模式的水平位置和方向
 * rotate_angle: 旋转模式的累计角度
 * orbit_angle:  环绕粒子的角度
 * wave_val/wave_dir: 波形进度条的当前值和方向
 * ================================================================ */
typedef enum {
    MODE_WAVE = 0,   /* 脉冲模式 — 方块大小周期性变化 */
    MODE_BOUNCE,     /* 弹跳模式 — 方块左右反弹 */
    MODE_ROTATE,     /* 旋转模式 — 方块自转 */
    MODE_COUNT       /* 模式总数 (用于循环切换) */
} anim_mode_t;

static anim_mode_t current_mode = MODE_WAVE;
static int anim_speed = 2;          /* 速度 1–5 */
static uint32_t frame_count = 0;    /* FPS: 当前秒内的帧计数 */
static int64_t last_fps_time = 0;   /* FPS: 上一次统计的时间戳 (微秒) */
static int current_fps = 0;         /* FPS: 当前帧率值 */

/* LVGL 控件指针 — 在 UI 创建时初始化, 动画回调中更新 */
static lv_obj_t *fps_label = NULL;
static lv_obj_t *anim_block = NULL;
static lv_obj_t *orbit_dot = NULL;
static lv_obj_t *wave_bar1 = NULL;
static lv_obj_t *wave_bar2 = NULL;
static lv_obj_t *mode_label = NULL;
static lv_obj_t *speed_label = NULL;

/* 动画状态变量 */
static float anim_phase = 0.0f;       /* 主相位 [0,1), 决定颜色和动画进度 */
static float bounce_x = 0.0f;         /* 弹跳 X 偏移 */
static float bounce_dir = 1.0f;       /* 弹跳方向 (+1 右 / -1 左) */
static float rotate_angle = 0.0f;     /* 旋转角度 (度) */
static float orbit_angle = 0.0f;      /* 环绕粒子角度 (弧度) */
static int wave_val1 = 0;             /* 进度条 1 值 */
static int wave_val2 = 0;             /* 进度条 2 值 */
static int wave_dir1 = 1;             /* 进度条 1 方向 */
static int wave_dir2 = -1;            /* 进度条 2 方向 (反相) */

/* 按键去抖状态 — ISR 中写入, 动画回调中读取并清零 */
static volatile uint32_t btn_state = 0;       /* 最后按下的 GPIO 编号 (0=无) */
static volatile uint32_t btn_last_tick = 0;   /* 上次触发时的 tick 值 */

/* ================================================================
 * 按键输入 — GPIO 中断 → 全局变量 → LVGL 定时器轮询
 *
 * 设计:
 *   按键按下 → GPIO 负边沿中断 → ISR 记录 GPIO 编号到 btn_state
 *   → anim_timer_cb (LVGL 定时器, 50Hz) 读取 btn_state 并清零
 *   → 更新模式/速度
 *
 * 优点: 不在 ISR 中直接操作 LVGL (LVGL 不是线程安全的),
 *      通过全局变量 + 轮询的方式解耦中断和 UI 更新。
 *
 * 去抖: 150ms 内重复触发被忽略 (BTN_DEBOUNCE_MS)
 * ================================================================ */
#define BTN_DEBOUNCE_MS 150   /* 去抖时间 (毫秒) */

/* ISR 中执行 — 仅记录按下事件, 不做复杂处理 */
static void IRAM_ATTR btn_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    uint32_t now = xTaskGetTickCountFromISR();
    /* 简单去抖: 距离上次触发不足 150ms 则忽略 */
    if (now - btn_last_tick > pdMS_TO_TICKS(BTN_DEBOUNCE_MS)) {
        btn_last_tick = now;
        btn_state = gpio_num;   /* 存入全局变量, 由 LVGL 定时器消费 */
    }
}

static void btn_init(void)
{
    /* 6 个按键统一配置: 输入 + 上拉 + 负边沿中断 */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_DOWN) |
                        (1ULL << PIN_BTN_LEFT) | (1ULL << PIN_BTN_RIGHT) |
                        (1ULL << PIN_BTN_A) | (1ULL << PIN_BTN_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,        /* 内部上拉 — 未按下时高电平 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,           /* 按下→低电平→下降沿触发 */
    };
    gpio_config(&cfg);

    gpio_install_isr_service(0);  /* 安装 GPIO 中断服务 (ESP_INTR_FLAG_DEFAULT) */
    gpio_isr_handler_add(PIN_BTN_UP,    btn_isr_handler, (void *)PIN_BTN_UP);
    gpio_isr_handler_add(PIN_BTN_DOWN,  btn_isr_handler, (void *)PIN_BTN_DOWN);
    gpio_isr_handler_add(PIN_BTN_LEFT,  btn_isr_handler, (void *)PIN_BTN_LEFT);
    gpio_isr_handler_add(PIN_BTN_RIGHT, btn_isr_handler, (void *)PIN_BTN_RIGHT);
    gpio_isr_handler_add(PIN_BTN_A,     btn_isr_handler, (void *)PIN_BTN_A);
    gpio_isr_handler_add(PIN_BTN_B,     btn_isr_handler, (void *)PIN_BTN_B);

    ESP_LOGI(TAG, "按键初始化完成");
}

/* ================================================================
 * FPS 帧率统计
 *
 * 每帧调用一次, 每秒更新一次显示:
 *   ≥30 FPS → 绿色 (流畅)
 *   20-29   → 黄色 (一般)
 *   <20     → 红色 (卡顿)
 * ================================================================ */
static void update_fps(void)
{
    frame_count++;
    int64_t now = esp_timer_get_time();          /* 微秒级时间戳 */
    if (now - last_fps_time >= 1000000) {        /* 每隔 1 秒 */
        current_fps = frame_count;
        frame_count = 0;
        last_fps_time = now;

        char buf[16];
        snprintf(buf, sizeof(buf), "%d FPS", current_fps);
        lv_label_set_text(fps_label, buf);

        /* 根据帧率变色 */
        if (current_fps >= 30) {
            lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00FF00), 0);  /* 绿 */
        } else if (current_fps >= 20) {
            lv_obj_set_style_text_color(fps_label, lv_color_hex(0xFFFF00), 0);  /* 黄 */
        } else {
            lv_obj_set_style_text_color(fps_label, lv_color_hex(0xFF0000), 0);  /* 红 */
        }
    }
}

/* ================================================================
 * 彩虹色生成 — 用三个相位差 120° 的正弦波合成 RGB
 *
 * phase 范围 [0, 1) 对应色环一圈
 * R/G/B 通道各相差 120° (2π/3 ≈ 2.094 / 4.189 rad)
 * ================================================================ */
static lv_color_t rainbow_color(float phase)
{
    /* sin 输出 [-1, +1] → 缩放平移至 [0, 255] */
    float r = sinf(phase * 2.0f * M_PI) * 127.0f + 128.0f;
    float g = sinf(phase * 2.0f * M_PI + 2.094f) * 127.0f + 128.0f;
    float b = sinf(phase * 2.0f * M_PI + 4.189f) * 127.0f + 128.0f;

    return lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/* ================================================================
 * 动画定时器回调 — 50Hz (每 20ms 调用一次)
 *
 * 职责:
 *   1. 消费按键事件 → 更新模式/速度
 *   2. 根据当前模式计算动画方块的位置/大小/颜色/旋转
 *   3. 更新环绕粒子位置
 *   4. 更新波形进度条
 *   5. 更新 FPS 统计
 *
 * 所有 LVGL API 调用在此回调中是安全的,
 * 因为 LVGL 定时器在 LVGL 任务上下文中执行。
 * ================================================================ */
static void anim_timer_cb(lv_timer_t *timer)
{
    /* ── 1. 处理按键输入 ── */
    uint32_t btn = btn_state;
    if (btn != 0) {
        btn_state = 0;  /* 消费事件 (清零, 避免重复处理) */

        switch (btn) {
        case PIN_BTN_LEFT:
            /* 循环左移: 0→2→1→0 (WAVE→ROTATE→BOUNCE→WAVE) */
            current_mode = (current_mode + MODE_COUNT - 1) % MODE_COUNT;
            break;
        case PIN_BTN_RIGHT:
            /* 循环右移: 0→1→2→0 (WAVE→BOUNCE→ROTATE→WAVE) */
            current_mode = (current_mode + 1) % MODE_COUNT;
            break;
        case PIN_BTN_UP:
            if (anim_speed < 5) anim_speed++;   /* 加速, 最大 5x */
            break;
        case PIN_BTN_DOWN:
            if (anim_speed > 1) anim_speed--;   /* 减速, 最小 1x */
            break;
        }

        /* 更新 UI 上的模式名称 */
        const char *mode_names[] = {"WAVE", "BOUNCE", "ROTATE"};
        lv_label_set_text(mode_label, mode_names[current_mode]);

        /* 更新 UI 上的速度显示 */
        char spd[8];
        snprintf(spd, sizeof(spd), "%dx", anim_speed);
        lv_label_set_text(speed_label, spd);
    }

    /* ── 2. 相位推进 (基础步长 × 速度倍率) ──
     * step=0.02 → 50 帧完成一个相位周期 (约 1 秒) */
    float step = 0.02f * (float)anim_speed;
    anim_phase += step;
    if (anim_phase > 1.0f) anim_phase -= 1.0f;

    /* ── 3. 动画方块 — 屏幕中央偏上区域 (bx,by) 为方块中心 ── */
    int bx = 0, by = 0;
    int block_size = 36;  /* 基准大小 */

    switch (current_mode) {

    case MODE_WAVE: {
        /* 脉冲模式: 方块大小按正弦波周期性变化 (26~46) */
        float pulse = sinf(anim_phase * 3.0f * M_PI) * 10.0f + block_size;
        int sz = (int)pulse;
        lv_obj_set_size(anim_block, sz, sz);
        bx = 80;   /* 水平居中: 160/2 */
        by = 44;   /* 垂直偏上: (128-进度条区域)/2 ≈ 44 */
        lv_obj_set_pos(anim_block, bx - sz/2, by - sz/2);
        lv_obj_set_style_bg_color(anim_block, rainbow_color(anim_phase), 0);
        /* 边框颜色偏移半个相位, 产生对比效果 */
        lv_obj_set_style_border_color(anim_block, rainbow_color(anim_phase + 0.5f), 0);
        break;
    }

    case MODE_BOUNCE: {
        /* 弹跳模式: 方块在 ±60px 范围内左右移动, 同时上下微振 */
        bounce_x += step * 200.0f * bounce_dir;      /* 水平速度 200 px/周期 */
        if (bounce_x > 60.0f)  { bounce_x = 60.0f;  bounce_dir = -1.0f; }  /* 碰右壁反弹 */
        if (bounce_x < -60.0f) { bounce_x = -60.0f; bounce_dir = 1.0f; }   /* 碰左壁反弹 */
        bx = 80 + (int)bounce_x;
        by = 44 + (int)(sinf(anim_phase * 2.0f * M_PI) * 15.0f);  /* Y 轴 ±15px 微振 */
        lv_obj_set_size(anim_block, block_size, block_size);
        lv_obj_set_pos(anim_block, bx - block_size/2, by - block_size/2);
        lv_obj_set_style_bg_color(anim_block, rainbow_color(anim_phase), 0);
        break;
    }

    case MODE_ROTATE: {
        /* 旋转模式: 方块原地自转 (LVGL 支持旋转变换) */
        rotate_angle += step * 360.0f;    /* 每周期转一圈 (360°) */
        lv_obj_set_size(anim_block, block_size, block_size);
        bx = 80;
        by = 44;
        lv_obj_set_pos(anim_block, bx - block_size/2, by - block_size/2);
        /* LVGL 旋转角度单位是 0.1°, 所以乘以 10 */
        lv_obj_set_style_transform_rotation(anim_block, (int)(rotate_angle * 10), 0);
        lv_obj_set_style_bg_color(anim_block, rainbow_color(anim_phase), 0);
        break;
    }

    default:
        break;
    }

    /* ── 4. 环绕粒子 — 以方块中心为圆心旋转 ── */
    orbit_angle += step * 4.0f * M_PI;    /* 粒子转得比主相位快 */
    int orbit_r = block_size/2 + 18;      /* 轨道半径 = 方块半径 + 18px */
    int ox = bx + (int)(cosf(orbit_angle) * orbit_r);
    int oy = by + (int)(sinf(orbit_angle) * orbit_r);
    lv_obj_set_pos(orbit_dot, ox - 4, oy - 4);  /* 8×8 粒子, 偏移半径到左上角 */
    lv_obj_set_style_bg_color(orbit_dot, rainbow_color(anim_phase + 0.3f), 0);

    /* ── 5. 波形进度条 (底部) — 两个反向振荡的进度条 ── */
    wave_val1 += wave_dir1 * 2 * anim_speed;
    wave_val2 += wave_dir2 * 2 * anim_speed;
    if (wave_val1 >= 100) { wave_val1 = 100; wave_dir1 = -1; }  /* 到达右端 → 反转 */
    if (wave_val1 <= 0)   { wave_val1 = 0;   wave_dir1 = 1; }   /* 到达左端 → 反转 */
    if (wave_val2 >= 100) { wave_val2 = 100; wave_dir2 = -1; }
    if (wave_val2 <= 0)   { wave_val2 = 0;   wave_dir2 = 1; }

    lv_bar_set_value(wave_bar1, wave_val1, LV_ANIM_OFF);   /* 无动画过渡, 直接设置 */
    lv_bar_set_value(wave_bar2, wave_val2, LV_ANIM_OFF);

    /* ── 6. 更新 FPS ── */
    update_fps();
}

/* ================================================================
 * UI 创建 — 在 160×128 横屏上布局所有控件
 *
 * 布局说明:
 *   ┌──────────────────────────────┐  (0,0)
 *   │ FPS                [160×128] │
 *   │    ┌────┐                    │
 *   │    │ 36 │  ◉ 动画区居中      │  动画方块 36×36, 环绕粒子 8×8
 *   │    └────┘                    │
 *   │                              │
 *   │ ▓▓▓▓▓▓▓▓  ▓▓▓▓▓▓▓▓        │  双波形进度条 (底部)
 *   │ WAVE              2x        │  模式标签 + 速度标签
 *   └──────────────────────────────┘  (159,127)
 * ================================================================ */
static void create_ui(lv_obj_t *scr)
{
    /* 深色背景 (#101520) */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101520), 0);

    /* ── FPS 标签 (左上角) ── */
    fps_label = lv_label_create(scr);
    lv_label_set_text(fps_label, "-- FPS");
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_14, 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_LEFT, 2, 2);

    /* ── 动画方块 (屏幕中央偏上区域) ──
     * 初始大小 36×36 像素, 圆角 6px, 橙色填充 + 金色边框 */
    anim_block = lv_obj_create(scr);
    lv_obj_set_size(anim_block, 36, 36);
    lv_obj_set_style_bg_color(anim_block, lv_color_hex(0xFF4400), 0);
    lv_obj_set_style_border_color(anim_block, lv_color_hex(0xFFAA00), 0);
    lv_obj_set_style_border_width(anim_block, 3, 0);
    lv_obj_set_style_radius(anim_block, 6, 0);
    lv_obj_set_style_bg_opa(anim_block, LV_OPA_COVER, 0);     /* 完全不透明 */
    lv_obj_remove_flag(anim_block, LV_OBJ_FLAG_SCROLLABLE);    /* 禁止滚动 */
    lv_obj_set_pos(anim_block, 62, 26);  /* 居中: (160-36)/2=62, (128-36)/2-20=26 */

    /* ── 环绕粒子 (8×8 圆点, 围绕动画方块旋转) ── */
    orbit_dot = lv_obj_create(scr);
    lv_obj_set_size(orbit_dot, 8, 8);
    lv_obj_set_style_bg_color(orbit_dot, lv_color_hex(0x00CCFF), 0);
    lv_obj_set_style_radius(orbit_dot, LV_RADIUS_CIRCLE, 0);   /* 正圆形 */
    lv_obj_set_style_border_width(orbit_dot, 0, 0);            /* 无边框 */
    lv_obj_set_style_bg_opa(orbit_dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(orbit_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(orbit_dot, 110, 30);

    /* ── 波形进度条 1 (底部左侧, 65×12, 青色指示器) ── */
    wave_bar1 = lv_bar_create(scr);
    lv_obj_set_size(wave_bar1, 65, 12);
    lv_bar_set_range(wave_bar1, 0, 100);
    lv_bar_set_value(wave_bar1, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(wave_bar1, lv_color_hex(0x303040), 0);       /* 底色 */
    lv_obj_set_style_bg_color(wave_bar1, lv_color_hex(0x00CCFF), LV_PART_INDICATOR);  /* 指示器色 */
    lv_obj_set_style_radius(wave_bar1, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(wave_bar1, 4, LV_PART_INDICATOR);
    lv_obj_align(wave_bar1, LV_ALIGN_BOTTOM_LEFT, 4, -22);

    /* ── 波形进度条 2 (底部右侧, 65×12, 品红色指示器) ── */
    wave_bar2 = lv_bar_create(scr);
    lv_obj_set_size(wave_bar2, 65, 12);
    lv_bar_set_range(wave_bar2, 0, 100);
    lv_bar_set_value(wave_bar2, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(wave_bar2, lv_color_hex(0x303040), 0);
    lv_obj_set_style_bg_color(wave_bar2, lv_color_hex(0xFF44AA), LV_PART_INDICATOR);
    lv_obj_set_style_radius(wave_bar2, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(wave_bar2, 4, LV_PART_INDICATOR);
    lv_obj_align(wave_bar2, LV_ALIGN_BOTTOM_RIGHT, -4, -22);

    /* ── 模式标签 (左下角, 显示 "WAVE"/"BOUNCE"/"ROTATE") ── */
    mode_label = lv_label_create(scr);
    lv_label_set_text(mode_label, "WAVE");
    lv_obj_set_style_text_color(mode_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(mode_label, &lv_font_montserrat_14, 0);
    lv_obj_align(mode_label, LV_ALIGN_BOTTOM_LEFT, 4, -4);

    /* ── 速度标签 (右下角, 显示 "1x"~"5x") ── */
    speed_label = lv_label_create(scr);
    lv_label_set_text(speed_label, "2x");
    lv_obj_set_style_text_color(speed_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(speed_label, &lv_font_montserrat_14, 0);
    lv_obj_align(speed_label, LV_ALIGN_BOTTOM_RIGHT, -4, -4);

    ESP_LOGI(TAG, "UI 创建完成");
}

/* ================================================================
 * 显示初始化 — ST7735 通过 esp_lcd 框架
 *
 * 流程:
 *   1. 初始化 SPI 总线 (VSPI, 40MHz, DMA)
 *   2. 创建 LCD 面板 IO 句柄 (SPI 4 线模式, 8 位命令/参数)
 *   3. 用 ST7789 驱动创建面板 (指令集兼容 ST7735)
 *   4. 发送 ST7735 专属初始化序列 (Gamma / 电源 / 帧率)
 *   5. 设置旋转: swap_xy + mirror_x → 横屏 (MADCTL = 0x60)
 *   6. 打开显示
 *
 * 【MADCTL 寄存器详解】
 *   寄存器地址 0x36, ST7735/ST7789 通用:
 *     Bit 7 (MY):  行地址方向    0=上→下    1=下→上
 *     Bit 6 (MX):  列地址方向    0=左→右    1=右→左
 *     Bit 5 (MV):  行列交换      0=不交换   1=交换(横屏)
 *     Bit 4 (ML):  垂直刷新方向  0=上→下    1=下→上
 *     Bit 3 (BGR): 颜色顺序      0=RGB      1=BGR
 *
 *   横屏(旋转90°): MADCTL = 0x60 → MX=1(右→左) | MV=1(交换)
 *
 * 【⚠️ 重要】此函数中设置的 swap_xy/mirror 状态必须与后续 lvgl_init()
 *   中的 rotation.swap_xy/rotation.mirror_x 完全一致！
 * ================================================================ */
static void display_init(esp_lcd_panel_io_handle_t *io_out, esp_lcd_panel_handle_t *panel_out)
{
    ESP_LOGI(TAG, "初始化 SPI 总线 (VSPI host=2)...");

    /* ── 1. 初始化 SPI 总线 ── */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,     /* 数据输出 */
        .miso_io_num = PIN_SPI_MISO,     /* 数据输入 (ST7735 不需要, 但必须指定) */
        .sclk_io_num = PIN_SPI_SCLK,     /* 时钟 */
        .quadwp_io_num = -1,             /* 不使用 Quad-SPI */
        .quadhd_io_num = -1,             /* 不使用 Quad-SPI */
        .max_transfer_sz = SCR_HOR_RES * SCR_VER_RES * 2,  /* 最大单次传输 = 一帧数据量 */
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_ID, &bus_cfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI 总线初始化完成");

    /* ── 2. 创建 LCD 面板 IO (SPI 4 线接口) ── */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_TFT_CS,       /* 片选引脚 */
        .dc_gpio_num = PIN_TFT_DC,       /* 数据/命令选择引脚 */
        .spi_mode = 0,                   /* CPOL=0, CPHA=0 — ST7735 标准模式 */
        .pclk_hz = SPI_CLK_HZ,           /* 像素时钟 = 40MHz */
        .trans_queue_depth = 10,         /* 传输队列深度 (允许排队 10 个 SPI 事务) */
        .lcd_cmd_bits = 8,               /* 命令占 8 位 */
        .lcd_param_bits = 8,             /* 参数占 8 位 */
    };

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST_ID, &io_cfg, &io_handle));
    ESP_LOGI(TAG, "LCD 面板 IO 创建完成");

    /* ── 3. 创建 LCD 面板 (使用 ST7789 驱动, 兼容 ST7735) ──
     * ST7789 和 ST7735 共享相同的指令集架构 (MIPI DCS),
     * 仅在 gamma/电源寄存器细节上有差异。
     *
     * rgb_ele_order = RGB: MADCTL 初始值 = 0x00 (BGR 位清零)
     *   → 后续 swap_xy/mirror 在此基础上设置 MV/MX 位
     *
     * bits_per_pixel = 16: COLMOD = 0x55 (RGB565 格式)
     *   → 每个像素 16 位: R[4:0]|G[5:3] 高字节, G[2:0]|B[4:0] 低字节
     */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_TFT_RST,        /* -1 = 无硬件复位引脚 */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,  /* 像素颜色顺序: 红→绿→蓝 */
        .bits_per_pixel = 16,                 /* RGB565 = 16bpp */
    };

    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));
    ESP_LOGI(TAG, "ST7735 面板创建完成 (通过 st7789 驱动)");

    /* ── 4. 复位 + 标准初始化 ──
     * esp_lcd_panel_init() 发送: SLPOUT→MADCTL→COLMOD→RAMCTRL
     * 这些是 ST7789 的标准初始化序列, ST7735 也接受但不够 */
    if (PIN_TFT_RST >= 0) {
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));  /* 硬件复位 (如有) */
    }
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));       /* 软件初始化 */

    /* ── 5. ST7735 专属初始化序列 ──
     * ST7789 驱动只发了 4 条通用指令, ST7735 必须额外配置以下寄存器
     * 才能正常显示。这些值来自 ST7735S 数据手册的推荐配置。
     *
     * 不同面板批次可能需要微调 gamma 值 (0xE0/0xE1) */
    /* 帧率控制: 约 60Hz */
    esp_lcd_panel_io_tx_param(io_handle, 0xB1, (uint8_t[]){0x01, 0x2C, 0x2D}, 3);  /* FRMCTR1 */
    esp_lcd_panel_io_tx_param(io_handle, 0xB2, (uint8_t[]){0x01, 0x2C, 0x2D}, 3);  /* FRMCTR2 */
    esp_lcd_panel_io_tx_param(io_handle, 0xB3, (uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6);  /* FRMCTR3 */
    /* 反转控制: 点反转 (dot inversion) — 减少闪烁 */
    esp_lcd_panel_io_tx_param(io_handle, 0xB4, (uint8_t[]){0x07}, 1);  /* INVCTR */
    /* 电源控制 1-5 + VCOM 控制 */
    esp_lcd_panel_io_tx_param(io_handle, 0xC0, (uint8_t[]){0xA2, 0x02, 0x84}, 3);  /* PWCTR1: AVDD=5.0V, GVDD=4.6V */
    esp_lcd_panel_io_tx_param(io_handle, 0xC1, (uint8_t[]){0xC5}, 1);             /* PWCTR2: VGH=15V, VGL=-10V */
    esp_lcd_panel_io_tx_param(io_handle, 0xC2, (uint8_t[]){0x0A, 0x00}, 2);       /* PWCTR3: 运放电流 */
    esp_lcd_panel_io_tx_param(io_handle, 0xC3, (uint8_t[]){0x8A, 0x2A}, 2);       /* PWCTR4: VCOM 参考电压 */
    esp_lcd_panel_io_tx_param(io_handle, 0xC4, (uint8_t[]){0x8A, 0xEE}, 2);       /* PWCTR5 */
    esp_lcd_panel_io_tx_param(io_handle, 0xC5, (uint8_t[]){0x0E}, 1);             /* VMCTR1: VCOMH=4.25V, VCOML=-1.5V */
    /* Gamma 校正 — 正极性 (PVGAM) 16 字节 */
    esp_lcd_panel_io_tx_param(io_handle, 0xE0, (uint8_t[]){
        0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22,
        0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10
    }, 16);
    /* Gamma 校正 — 负极性 (NVGAM) 16 字节 */
    esp_lcd_panel_io_tx_param(io_handle, 0xE1, (uint8_t[]){
        0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E,
        0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10
    }, 16);
    /* 正常显示模式 (退出部分显示模式) */
    esp_lcd_panel_io_tx_param(io_handle, 0x13, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "ST7735 gamma & 电源初始化完成");

    /* ── 6. 设置旋转方向 ──
     * 物理 128×160 竖屏 → 旋转 90° → 逻辑 160×128 横屏
     *
     * swap_xy(true):  交换行列地址 (MV=1) → X 轴与 Y 轴互换
     * mirror(true, false): X 镜像 (MX=1), Y 不镜像 (MY=0)
     *
     * 最终 MADCTL = MX|MV = 0x40|0x20 = 0x60
     *
     * ⚠️ 下面的状态必须与 lvgl_init() 中 rotation 配置完全一致 */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));

    /* ── 7. 打开显示 ── */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "显示初始化完成: %dx%d 横屏", SCR_HOR_RES, SCR_VER_RES);
    *io_out = io_handle;
    *panel_out = panel_handle;
}

/* ================================================================
 * LVGL 初始化 — 创建 LVGL 任务并注册显示设备
 *
 * 【配置要点】
 *   - buffer_size:     填"像素数" (不是字节数! 内部会自动乘颜色深度)
 *   - rotation:        必须与 display_init() 中面板的 swap_xy/mirror 完全一致
 *   - buff_spiram:     ESP32 上不要与 buff_dma 同时为 true (PSRAM DMA bug)
 *   - swap_bytes:      小端 CPU → 大端 LCD 的字节序转换
 *
 * 【数据流】
 *   LVGL 渲染 (→小端 RGB565) → swap_bytes (→大端) → DMA → SPI → ST7735
 * ================================================================ */
static void lvgl_init(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel)
{
    ESP_LOGI(TAG, "初始化 LVGL 移植层...");

    /* ── 1. 创建 LVGL 任务 ──
     * task_priority=5:  中等优先级, 低于系统关键任务但高于普通应用任务
     * task_stack=8192:  8KB 栈空间 (LVGL v9 推荐至少 4KB, UI 复杂时可加大)
     * task_affinity=1:  绑定到 CPU Core 1, 与主循环 (Core 0) 分离, 避免阻塞
     * timer_period_ms=5: LVGL 内部定时器精度 5ms (200Hz), 值越小动画越流畅 */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 5,
        .task_stack = 8192,
        .task_affinity = 1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* ── 2. 注册显示设备 ── */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,       /* SPI IO 句柄 (命令/数据传输) */
        .panel_handle = panel,        /* LCD 面板句柄 (方向/开关控制) */

        /* buffer_size 是像素数: 160×128 = 20480 px
         * 内部分配: 20480 px × 2 B/px × 2 缓冲 = 81920 字节 ≈ 80KB
         * ⚠️ 不要写 bytes! 单位是 pixels! */
        .buffer_size = SCR_HOR_RES * SCR_VER_RES,

        .double_buffer = true,        /* 双缓冲: 一个渲染, 一个 DMA 传输 */
        .hres = SCR_HOR_RES,          /* 水平分辨率 160 */
        .vres = SCR_VER_RES,          /* 垂直分辨率 128 */
        .monochrome = false,          /* 非单色屏 */
        .color_format = LV_COLOR_FORMAT_RGB565,  /* 16 位色 RGB565 */

        /* ⚠️ 旋转配置 — 必须与 display_init() 中的设置严格一致!
         *
         * display_init() 中:
         *   esp_lcd_panel_swap_xy(true)   → swap_xy = true
         *   esp_lcd_panel_mirror(true, false) → mirror_x = true, mirror_y = false
         *
         * LVGL 端口初始化时会根据这里的值调用相同的函数,
         * 如果不一致会导致面板方向被覆盖! */
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },

        .flags = {
            .buff_dma = true,         /* 使用 DMA 传输 (速度快) */

            /* ⚠️ ESP32 上 PSRAM + DMA 有缓存一致性问题:
             * CPU 通过 cache 写 PSRAM, DMA 直接读 PSRAM,
             * 如果 cache 未回写, DMA 读到过期数据 → 花屏
             * 解决方案: 将缓冲区放在内部 SRAM (DMA 可直接访问) */
            .buff_spiram = false,

            /* ⚠️ 字节序转换:
             * ESP32 是小端 (LE), ST7735 默认大端 (BE)
             * swap_bytes=true 在 flush 时交换每对字节
             *   LE [B0,B1] → BE [B1,B0] */
            .swap_bytes = true,
        },
    };

    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "LVGL 显示设备添加失败!");
        abort();
    }

    ESP_LOGI(TAG, "LVGL 初始化完成");
}

/* ================================================================
 * 主函数
 *
 * 初始化顺序:
 *   1. 显示 (SPI + ST7735)
 *   2. LVGL (任务 + 显示设备注册)
 *   3. UI (创建控件)
 *   4. 按键 (GPIO 中断)
 *   5. 动画定时器 (50Hz)
 *
 * ⚠️ LVGL 操作必须在 lvgl_port_lock/unlock 之间
 *    LVGL 不是线程安全的, 这些锁确保与 LVGL 任务互斥
 * ================================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "学而思编程机 — LVGL 动画演示");
    ESP_LOGI(TAG, "ESP-IDF v%d.%d, ESP32-WROVER-B", 5, 4);
    ESP_LOGI(TAG, "==========================================");

    /* 1. 初始化显示 */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    display_init(&io_handle, &panel);

    /* 2. 初始化 LVGL */
    lvgl_init(io_handle, panel);

    /* 3. 创建 UI — 必须在 LVGL 锁内操作 */
    lvgl_port_lock(0);
    create_ui(lv_screen_active());
    lvgl_port_unlock();

    /* 4. 初始化按键输入 */
    btn_init();

    /* 5. 创建动画定时器: 20ms 周期 = 50Hz 刷新率 */
    lvgl_port_lock(0);
    lv_timer_create(anim_timer_cb, 20, NULL);
    lvgl_port_unlock();

    /* 校准 FPS 计时的起始点 */
    last_fps_time = esp_timer_get_time();

    ESP_LOGI(TAG, "演示程序运行中! 使用方向键控制:");
    ESP_LOGI(TAG, "  左右键: 切换模式 (WAVE 脉冲 / BOUNCE 弹跳 / ROTATE 旋转)");
    ESP_LOGI(TAG, "  上下键: 调节速度 (1x ~ 5x)");

    /* 主循环 — LVGL 在自己的任务中处理所有渲染和动画
     * 这里只需保持主任务不退出 (FreeRTOS 不允许 app_main 返回) */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
