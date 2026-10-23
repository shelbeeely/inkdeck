#pragma once
typedef void* SemaphoreHandle_t;
typedef unsigned TickType_t;
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) (x)
inline void vTaskDelay(TickType_t) {}
inline int xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned, void*) { return 1; }
