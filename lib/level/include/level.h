#ifndef LEVEL_H
#define LEVEL_H

#include <stdint.h>

// Level meter: DC blocker + RMS over a 32 ms window, in dBFS (0 dBFS = 24-bit full scale).
// One instance; state is kept inside the module.

// must be called before level_process; sample_rate_hz must not be 0
void level_init(uint32_t sample_rate_hz);

// feeds count 24-bit samples; returns true and writes *dbfs_out when a window completes
// returns false and leaves *dbfs_out unchanged otherwise, or if level_init was not called
bool level_process(const int32_t *samples, uint32_t count, float *dbfs_out);

#endif /* LEVEL_H */
