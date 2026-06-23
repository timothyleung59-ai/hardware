/*
 * Hello World — ESP32-S3-RLCD-4.2
 *
 * RLCD 屏幕显示 "Hello World" + SHTC3 温湿度
 * 从零开始的第一个 ESP-IDF 项目
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#include "u8g2_st7305.h"

/* ── 硬件引脚定义 ────────────────────────────── */

// RLCD 屏幕 (SPI)
#define PIN_LCD_MOSI  GPIO_NUM_12
#define PIN_LCD_SCK   GPIO_NUM_11
#define PIN_LCD_DC    GPIO_NUM_5
#define PIN_LCD_CS    GPIO_NUM_40
#define PIN_LCD_RST   GPIO_NUM_41

// I2C 总线 (SHTC3 温湿度传感器)
#define PIN_I2C_SDA   GPIO_NUM_13
#define PIN_I2C_SCL   GPIO_NUM_14

// SHTC3 传感器
#define SHTC3_ADDR    0x70

/* ── 屏幕相关 ──────────────────────────────── */

#define LCD_W  400  // 横屏宽
#define LCD_H  300  // 横屏高

static u8g2_st7305_t g_lcd;
static const char *TAG = "hello";

/* ── SHTC3 传感器驱动 ─────────────────────────── */

static i2c_master_dev_handle_t shtc3_dev;

static esp_err_t shtc3_send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { cmd >> 8, cmd & 0xFF };
    return i2c_master_transmit(shtc3_dev, buf, 2, 100);
}

static esp_err_t shtc3_read(float *temp, float *humidity)
{
    // 测量: T first, normal mode, no clock stretching
    esp_err_t ret = shtc3_send_cmd(0x7866);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(30));

    // 读取 6 字节: temp(2) + crc(1) + humi(2) + crc(1)
    uint8_t data[6] = {0};
    ret = i2c_master_receive(shtc3_dev, data, 6, 200);
    if (ret != ESP_OK) return ret;

    uint16_t raw_t = (data[0] << 8) | data[1];
    uint16_t raw_h = (data[3] << 8) | data[4];

    *temp     = -45.0f + 175.0f * (float)raw_t / 65535.0f;
    *humidity = 100.0f * (float)raw_h / 65535.0f;
    return ESP_OK;
}

static esp_err_t shtc3_init(void)
{
    // 初始化 I2C 主机总线
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus init failed");

    // 添加 SHTC3 设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &shtc3_dev), TAG, "shtc3 add failed");

    // 唤醒传感器，之后保持唤醒状态
    shtc3_send_cmd(0x3517);
    vTaskDelay(pdMS_TO_TICKS(2));

    return ESP_OK;
}

/* ── 绘制函数 ─────────────────────────────── */

static void draw_centered(u8g2_t *u8g2, int y, const char *text)
{
    int w = u8g2_GetStrWidth(u8g2, text);
    int x = (LCD_W - w) / 2;
    if (x < 0) x = 0;
    u8g2_DrawStr(u8g2, x, y, text);
}

static void draw_screen(u8g2_t *u8g2, float temp, float humi, bool sensor_ok)
{
    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);

    // 外框
    u8g2_DrawRFrame(u8g2, 5, 5, LCD_W - 10, LCD_H - 10, 8);

    // 标题
    u8g2_SetFont(u8g2, u8g2_font_logisoso32_tf);
    draw_centered(u8g2, 70, "Hello World!");

    // 分隔线
    u8g2_DrawHLine(u8g2, 40, 90, LCD_W - 80);

    // 副标题
    u8g2_SetFont(u8g2, u8g2_font_6x13_tf);
    draw_centered(u8g2, 120, "ESP32-S3-RLCD-4.2 | ESP-IDF v6.0.1");

    // 温湿度
    u8g2_SetFont(u8g2, u8g2_font_logisoso22_tf);
    char buf[64];
    if (sensor_ok) {
        snprintf(buf, sizeof(buf), "%.1f C  /  %.1f %%", temp, humi);
    } else {
        snprintf(buf, sizeof(buf), "Sensor: --");
    }
    draw_centered(u8g2, 180, buf);

    // 底部提示
    u8g2_SetFont(u8g2, u8g2_font_6x13_tf);
    draw_centered(u8g2, 250, "My first ESP-IDF project");

    u8g2_SendBuffer(u8g2);
}

/* ── 主函数 ───────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "Hello World from ESP32-S3!");

    // 1) 初始化屏幕
    u8g2_st7305_config_t lcd_cfg = u8g2_st7305_default_config();
    lcd_cfg.mosi_io  = PIN_LCD_MOSI;
    lcd_cfg.sclk_io  = PIN_LCD_SCK;
    lcd_cfg.dc_io    = PIN_LCD_DC;
    lcd_cfg.cs_io    = PIN_LCD_CS;
    lcd_cfg.reset_io = PIN_LCD_RST;
    lcd_cfg.rotation = U8G2_R1;  // 横屏

    ESP_ERROR_CHECK(u8g2_st7305_init(&g_lcd, &lcd_cfg));
    u8g2_t *u8g2 = u8g2_st7305_get_u8g2(&g_lcd);
    ESP_LOGI(TAG, "RLCD display initialized");

    // 2) 初始化温湿度传感器
    bool sensor_ok = (shtc3_init() == ESP_OK);
    if (sensor_ok) {
        ESP_LOGI(TAG, "SHTC3 sensor initialized");
    } else {
        ESP_LOGW(TAG, "SHTC3 sensor init failed, display will show '--'");
    }

    // 3) 主循环: 每 2 秒刷新一次
    float temp = 0, humi = 0;
    while (true) {
        if (sensor_ok) {
            if (shtc3_read(&temp, &humi) == ESP_OK) {
                ESP_LOGI(TAG, "Temp: %.1f C, Humidity: %.1f %%", temp, humi);
            }
        }

        draw_screen(u8g2, temp, humi, sensor_ok);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
