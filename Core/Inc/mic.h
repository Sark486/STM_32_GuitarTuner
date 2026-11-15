#ifndef MIC_H
#define MIC_H

#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include <stddef.h>
#include <stdint.h>

#define MIC_BLOCK_SAMPLES 1024U // 1024 / 32000 Hz = 32 ms, one hop

void mic_init(I2S_HandleTypeDef *hi2s);
HAL_StatusTypeDef mic_start(void);
uint32_t mic_get_overrun_count(void);

bool mic_read_block(int32_t *samples_out);
uint32_t mic_get_sample_rate_hz(void);
void mic_resync(void);
// keeps the top 16 of the 24 significant bits
void mic_to_int16(const int32_t *samples, int16_t *out, size_t count);

#endif /* MIC_H */
