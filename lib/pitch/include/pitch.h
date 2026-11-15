#ifndef PITCH_H
#define PITCH_H

#include <stdint.h>

/* Sample rates */

// input sample rate, Hz
#define PITCH_FS_IN 32000.0f
// 32 kHz to 8 kHz
#define PITCH_DECIM 4U
// decimated rate, 8000 Hz
#define PITCH_FS    (PITCH_FS_IN / PITCH_DECIM)

/* Blocks and windows */

// samples at 32 kHz per push, 8 ms
#define PITCH_BLOCK_SIZE       256U
// samples at 8 kHz per push, 8 ms
#define PITCH_BLOCK_SIZE_DECIM (PITCH_BLOCK_SIZE / PITCH_DECIM)
// analysis runs every 256 decimated samples (8 kHz), which is 32 ms or 4 pushes
#define PITCH_HOP              256U
// samples at 8 kHz: 128 ms
#define PITCH_WINDOW           1024U
// the same 128 ms at 32 kHz
#define PITCH_RAW_WINDOW       (PITCH_WINDOW * PITCH_DECIM)
// filter coefficients; must match fir_taps.h
#define PITCH_FIR_TAPS         119U

/* YIN search on the decimated window */

// floor(8000 / 1400): highest note 1400 Hz = 5.71 samples; rounding down leaves lag 4 as the left neighbour
#define PITCH_TAU_MIN 5U
// ceil(8000 / 29): search floor 29 Hz, about A#0, a semitone under B0, so a flat B0 still reads
#define PITCH_TAU_MAX 276U
// candidate periods 0 to TAU_MAX + 1; the extra one is the right-hand neighbour of the best candidate
#define PITCH_D_LEN   (PITCH_TAU_MAX + 2U)
// number of samples compared at each candidate period
#define PITCH_YIN_W   (PITCH_WINDOW - PITCH_TAU_MAX - 1)

/* Thresholds on d' (lower is better) */

// first lag with d' below this is the period dip
#define PITCH_YIN_THRESHOLD  0.15f
// confidence threshold, the values below this considered to have detected pitch
#define PITCH_CONF_THRESHOLD 0.15f
// result valid only if confidence <= this
#define PITCH_GATE           0.5f

/* Refinement at the full sample rate */

// 1 lag at 8 kHz is 4 lags at 32 kHz
#define PITCH_REFINE_SEARCH  PITCH_DECIM
// 1110: top of the 8 kHz search in 32 kHz lags plus space for the parabola and neighbours
#define PITCH_REFINE_MAX_LAG (PITCH_TAU_MAX * PITCH_DECIM + PITCH_DECIM + 2U)
// 2984: samples compared per lag
#define PITCH_REFINE_W       (PITCH_RAW_WINDOW - PITCH_REFINE_MAX_LAG - 2U)
// 11: center +- (search + 1)
#define PITCH_REFINE_LAGS    (2U * PITCH_REFINE_SEARCH + 3U)

typedef struct
{
  // always filled, use only if 'valid' is true; NaN when the period is not usable
  float freq_hz;
  // lower is better
  float confidence;
  // period in 8 kHz samples, before fine-tuning on 32 kHz
  float tau;
  // total number of 32 kHz samples pushed since pitch_init, including this block
  uint32_t sample_count;
  // true when the best period isn't at the edge of the search range, confidence <= PITCH_GATE
  // and the frequency is finite
  bool valid;
} pitch_result_t;

// fields are private
typedef struct
{
  // filter input, scaled to +-1.0: the last PITCH_FIR_TAPS - 1 samples of the previous block, then the current block
  float fir_delay[PITCH_FIR_TAPS - 1 + PITCH_BLOCK_SIZE];
  // the latest PITCH_WINDOW decimated samples (8 kHz, 128 ms), oldest first; YIN runs on it every hop
  float window[PITCH_WINDOW];
  // the latest PITCH_RAW_WINDOW input samples (32 kHz, 128 ms), oldest first, kept as int16 exactly as pushed;
  // the fine-tuning step uses them
  int16_t raw_window[PITCH_RAW_WINDOW];
  // the difference function for tau from 0 to PITCH_TAU_MAX + 1
  float d[PITCH_D_LEN];
  // normalised d
  float dprime[PITCH_D_LEN];
  // difference function at 32 kHz for the lags around the candidate period
  float d_refine[PITCH_REFINE_LAGS];
  // 32 kHz samples pushed since init; it becomes sample_count in the result
  uint32_t samples_in;
  // decimated samples received, counted until the window is full
  uint32_t filled;
  // decimated samples since the last analysis; analysis runs when it reaches PITCH_HOP
  uint32_t since_hop;
} pitch_t;

// inits with new state; must be executed before push
void pitch_init(pitch_t *state);

// returns true when a new result was written; *out is not changed when returned false
// returns false and doesn't change the state if count != PITCH_BLOCK_SIZE
// state, block, out must not be null
bool pitch_push(pitch_t *state, const int16_t *block, uint32_t count, pitch_result_t *out);

#endif /* PITCH_H */
