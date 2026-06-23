# Waveshare ESP32-S3-RLCD-4.2 Hardware Summary

## SoC

- Module: ESP32-S3-WROOM-1-N16R8
- CPU: Dual-core Xtensa LX7 @ 240 MHz
- Flash: 16 MB (Quad SPI)
- PSRAM: 8 MB (Octal SPI)
- Wi-Fi: 2.4 GHz 802.11 b/g/n
- Bluetooth: 5.0 LE

## Display — 4.2" Reflective LCD (RLCD)

- Driver IC: ST7305
- Resolution: 300 x 400 (portrait), typically used 400 x 300 (landscape, U8G2_R1)
- Color: Black & White (1-bit)
- Type: Fully reflective, no backlight required, sunlight readable
- Interface: SPI
- Graphics library: U8G2 (bundled component) or LVGL (v8/v9)

| Signal | GPIO |
|--------|------|
| MOSI   | 12   |
| SCK    | 11   |
| DC     | 5    |
| CS     | 40   |
| RST    | 41   |
| TE     | 6    |

SPI Host: SPI2_HOST

## Audio — Playback & Recording

### DAC (Speaker Output)
- Codec: ES8311
- I2C Address: 0x18
- Output: MX1.25 2PIN speaker connector (8 ohm, 2W recommended)
- PA enable: GPIO 46

### ADC (Microphone Input)
- Codec: ES7210 (4-channel ADC, 2 channels used)
- I2C Address: 0x40
- Input: 2x onboard MEMS microphones (dual-mic array)
- Mic gain range: 0 ~ 37.5 dB

### I2S Bus (shared by ES8311 & ES7210)

| Signal | GPIO |
|--------|------|
| MCLK   | 16   |
| BCLK   | 9    |
| WS     | 45   |
| DOUT   | 8    |
| DIN    | 10   |

## I2C Bus

| Signal | GPIO |
|--------|------|
| SDA    | 13   |
| SCL    | 14   |

All I2C peripherals share this bus (100~400 kHz).

### I2C Devices

| Device   | Address | Function                          |
|----------|---------|-----------------------------------|
| SHTC3    | 0x70    | Temperature & humidity sensor     |
| PCF85063 | 0x51    | RTC (real-time clock)             |
| ES8311   | 0x18    | Audio DAC (speaker)               |
| ES7210   | 0x40    | Audio ADC (microphone)            |

## TF Card (MicroSD)

- Interface: SDMMC (1-bit mode)

| Signal | GPIO |
|--------|------|
| CLK    | 38   |
| CMD    | 21   |
| D0     | 39   |

## Battery

- Holder: 18650 single-cell (onboard holder)
- Charging: Onboard charge circuit (USB-C input)
- Voltage sense: ADC1 Channel 3 (GPIO 4), 12-bit, voltage divider — multiply reading x2 for actual battery voltage

## Buttons

| Button | GPIO | Notes                              |
|--------|------|------------------------------------|
| BOOT   | 0    | Active low, internal pull-up       |
| KEY    | 18   | Active low, internal pull-up       |
| PWR    | —    | Hardware power: long press off, short press on |

No hardware RESET button. Enter download mode by holding BOOT while powering on.

## USB

- Type-C connector
- USB-CDC/JTAG (native USB on ESP32-S3)
- Used for flashing, serial monitor, and battery charging

## Power

- USB 5V or 18650 battery (3.7V)
- Onboard LDO regulation
- Low-power potential: RLCD has zero backlight power draw, RTC for timed wakeup

## Free GPIO (directly exposed or unused)

Most GPIO are allocated to onboard peripherals. Available pins depend on which peripherals are in use. Check the schematic for exposed pin headers.

## Software & SDK

- Primary SDK: ESP-IDF (v5.x / v6.x)
- Also supports: Arduino, ESPHome, XiaoZhi AI
- Display drivers: U8G2 (lightweight, B/W), LVGL v8/v9 (full GUI)
- Audio framework: espressif/esp_codec_dev component
- Official examples: Wi-Fi AP/STA, ADC battery, RTC, SHTC3 sensor, SD card, audio playback/recording, LVGL UI, U8G2 graphics

## Notes

- ESP-IDF v6.0+ requires patching Waveshare's example code: driver components split (add `esp_driver_gpio`, `esp_driver_i2c`, `esp_driver_spi`, `esp_driver_i2s` to CMakeLists.txt), GCC 15 stricter type checks (`gpio_num_t` casts), and some deprecated API constants.
- The RLCD is ideal for always-on displays (weather station, calendar, e-ink-like use cases) due to zero backlight power.
- Dual-mic array enables noise cancellation / beamforming for voice applications.
