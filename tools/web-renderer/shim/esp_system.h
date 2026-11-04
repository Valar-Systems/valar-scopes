#pragma once
#include <cstdint>
#include <cstdlib>

// Blipscope web renderer: ESP-IDF system calls. The demo always "booted from power-on".
typedef enum {
    ESP_RST_UNKNOWN = 0, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT,
    ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO, ESP_RST_USB, ESP_RST_JTAG, ESP_RST_EFUSE,
    ESP_RST_PWR_GLITCH, ESP_RST_CPU_LOCKUP
} esp_reset_reason_t;
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
inline uint32_t esp_random() { return (uint32_t)std::rand() * 2654435761u; }
inline uint32_t esp_get_free_heap_size() { return 180000; }
inline uint32_t esp_get_minimum_free_heap_size() { return 150000; }
void esp_restart();
