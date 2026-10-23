#pragma once
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (void*)1; }
inline int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
