#include "replay.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include "capture.h"
#include "cycles.h"
#include "debug_mark.h"
#include "pitch.h"

// whole capture buffer: 32768 samples / 256 per push
#define REPLAY_MAX_PUSHES  (CAPTURE_TOTAL_SAMPLES_COUNT / PITCH_BLOCK_SIZE)
// full buffer gives 29: first one after push 16, then every 4th push
#define REPLAY_MAX_RESULTS 32U

static uint32_t push_cycles[REPLAY_MAX_PUSHES];
static bool had_result[REPLAY_MAX_PUSHES]; // true if a result for this push was saved in results
static pitch_result_t results[REPLAY_MAX_RESULTS];

// min and max cycles separately for pushes with and without result
static void print_summary(size_t pushes)
{
  uint32_t min_plain = UINT32_MAX;
  uint32_t max_plain = 0U;
  uint32_t min_result = UINT32_MAX;
  uint32_t max_result = 0U;

  for (size_t i = 0; i < pushes; i++)
  {
    uint32_t c = push_cycles[i];
    if (had_result[i])
    {
      if (c < min_result)
      {
        min_result = c;
      }
      if (c > max_result)
      {
        max_result = c;
      }
    }
    else
    {
      if (c < min_plain)
      {
        min_plain = c;
      }
      if (c > max_plain)
      {
        max_plain = c;
      }
    }
  }
  printf("S,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\r\n", min_plain, max_plain, min_result, max_result);
}

void replay_run(pitch_t *state, const int16_t *samples, uint32_t count)
{
  uint32_t pushes = count / PITCH_BLOCK_SIZE;
  if (pushes > REPLAY_MAX_PUSHES)
  {
    pushes = REPLAY_MAX_PUSHES;
  }

  // fresh state for every replay
  pitch_init(state);
  uint32_t result_count = 0U;

  for (size_t i = 0; i < pushes; i++)
  {
    pitch_result_t result;

    DEBUG_MARK1(GPIO_PIN_SET);
    uint32_t start = cycles_get();
    bool got_result = pitch_push(state, samples + i * PITCH_BLOCK_SIZE, PITCH_BLOCK_SIZE, &result);
    uint32_t end = cycles_get();
    DEBUG_MARK1(GPIO_PIN_RESET);

    push_cycles[i] = end - start;
    had_result[i] = false;
    if (got_result && result_count < REPLAY_MAX_RESULTS)
    {
      results[result_count] = result;
      result_count++;
      had_result[i] = true;
    }
  }

  printf("REPLAY %" PRIu32 " %" PRIu32 "\r\n", pushes, result_count);
  size_t r = 0;
  for (size_t i = 0; i < pushes; i++)
  {
    // newlib-nano printf has no %zu, so the push index is printed as unsigned int
    printf("P,%u,%" PRIu32 "\r\n", (unsigned int)i, push_cycles[i]);
    if (had_result[i])
    {
      const pitch_result_t *p = &results[r];
      printf("R,%u,%" PRIu32 ",%.9g,%.9g,%.9g,%d\r\n", (unsigned int)i, p->sample_count, (double)p->freq_hz,
             (double)p->confidence, (double)p->tau, p->valid);
      r++;
    }
  }
  print_summary(pushes);
  printf("END\r\n");
}
