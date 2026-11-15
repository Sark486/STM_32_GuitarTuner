#ifndef DEBUG_MARK_H
#define DEBUG_MARK_H

#include "main.h"

// Marker pins for timing measurements with a logic analyser; built in only with TUNER_DEBUG_MARKS == ON.
// MARK1 (PB6): high while main processes one mic block, or during one pitch_push in the replay benchmark
// MARK2 (PB7): high while DMA fills the second half of the I2S buffer, low while it fills the first half
#if TUNER_DEBUG_MARKS
#define DEBUG_MARK1(state) HAL_GPIO_WritePin(MARK1_GPIO_Port, MARK1_Pin, (state))
#define DEBUG_MARK2(state) HAL_GPIO_WritePin(MARK2_GPIO_Port, MARK2_Pin, (state))
#else
#define DEBUG_MARK1(state) ((void)0)
#define DEBUG_MARK2(state) ((void)0)
#endif

#endif /* DEBUG_MARK_H */
