#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "note.h"
#define A4_HZ                 440.0f  // default concert pitch for most cases
#define TOLERANCE_CENTS       0.01f   // cents comparison tolerance, expected values computed in double precision
#define TOLERANCE_NAMES_CENTS 0.1f    // cents tolerance for the C4..B4 table, its frequencies are rounded to 0.01 Hz
#define SWEEP_F_MIN_HZ        29.0f   // lowest frequency the pitch detector reports
#define SWEEP_F_MAX_HZ        1400.0f // highest frequency the pitch detector reports
#define SWEEP_STEP_RATIO      1.0007f // ~1.2 cents per step, ~5500 steps
#define SWEEP_MAX_PRINTS      5U      // printed failures per sweep, the rest are only counted

static uint16_t failure_count = 0U;

static void test_check(bool result, const char *description)
{
  if (!result)
  {
    failure_count++;
    printf("FAIL: %s\n", description);
  }
}

typedef struct {
  const char *name;
  float freq_hz;
  float a4_hz;
  int32_t nearest;
  uint8_t pitch_class;
  float cents;
} freq_case_t;

// expected values from n = 69 + 12 * log2(f / A4)
static const freq_case_t freq_cases[] = {
  { "A4",              440.00f, 440.0f, 69,  9U,   0.000f },
  { "B0",               30.87f, 440.0f, 23, 11U,   0.129f },
  { "29 Hz",            29.00f, 440.0f, 22, 10U,  -8.054f },
  { "E2",               82.41f, 440.0f, 40,  4U,   0.065f },
  { "E4",              329.63f, 440.0f, 64,  4U,   0.013f },
  { "E2 sharp",         84.00f, 440.0f, 40,  4U,  33.149f },
  { "1400 Hz",        1400.00f, 440.0f, 89,  5U,   3.822f },
  // A4/A#4 midpoint is 452.893 Hz, checked from both sides
  { "below midpoint",  452.00f, 440.0f, 69,  9U,  46.583f },
  { "above midpoint",  454.00f, 440.0f, 70, 10U, -45.773f },
  { "A4 = 432",        440.00f, 432.0f, 69,  9U,  31.767f },
  { "A4 = 445",        440.00f, 445.0f, 69,  9U, -19.562f },
};
#define FREQ_CASES_COUNT (sizeof freq_cases / sizeof freq_cases[0])

static void freq_test(void)
{
  for (size_t i = 0; i < FREQ_CASES_COUNT; i++)
  {
    const freq_case_t *c = &freq_cases[i];
    note_result_t note = {};
    bool ok = note_from_freq(c->freq_hz, c->a4_hz, &note);

    // number must be consistent with nearest and cents
    float number_expected = (float)note.nearest + note.cents / 100.0f;
    if (!ok
        || note.nearest != c->nearest
        || note.pitch_class != c->pitch_class
        || fabsf(note.cents - c->cents) > TOLERANCE_CENTS
        || fabsf(note.number - number_expected) > TOLERANCE_CENTS / 100.0f)
    {
      printf("%s: returned %d, nearest actual: %d, expected: %d, class actual: %u, expected: %u, "
             "cents actual: %.3f, expected: %.3f, number: %.4f\n",
             c->name, (int)ok, (int)note.nearest, (int)c->nearest,
             (unsigned)note.pitch_class, (unsigned)c->pitch_class,
             (double)note.cents, (double)c->cents, (double)note.number);
      failure_count++;
    }
  }
}

/*
  One frequency per pitch class, C4..B4.
*/
static void classes_test(void)
{
  static const float octave4_hz[] = {
    261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f,
    369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f,
  };

  for (uint8_t i = 0U; i < 12U; i++)
  {
    note_result_t note = {};
    bool ok = note_from_freq(octave4_hz[i], A4_HZ, &note);
    if (!ok
        || note.pitch_class != i
        || note.nearest != 60 + (int32_t)i // C4 = 60
        || fabsf(note.cents) > TOLERANCE_NAMES_CENTS)
    {
      printf("%.2f Hz: returned %d, nearest actual: %d, expected: %d, class actual: %u, expected: %u, cents: %.3f\n",
             (double)octave4_hz[i], (int)ok, (int)note.nearest, 60 + (int)i,
             (unsigned)note.pitch_class, (unsigned)i, (double)note.cents);
      failure_count++;
    }
  }
}

/*
  Sweep over the detector range: cents in [-50, 50), class = nearest % 12, nearest never goes down.
*/
static void sweep_test(void)
{
  int32_t previous_nearest = 0;
  uint32_t sweep_failures = 0U;

  for (float f = SWEEP_F_MIN_HZ; f <= SWEEP_F_MAX_HZ; f *= SWEEP_STEP_RATIO)
  {
    note_result_t note = {};
    bool ok = note_from_freq(f, A4_HZ, &note);
    if (!ok
        || note.cents < -50.0f || note.cents >= 50.0f
        || note.pitch_class >= 12U
        || note.nearest <= 0
        || (int32_t)note.pitch_class != note.nearest % 12
        || note.nearest < previous_nearest)
    {
      if (sweep_failures < SWEEP_MAX_PRINTS)
      {
        printf("sweep, %.3f Hz: returned %d, number: %.4f, nearest: %d, previous: %d, class: %u, cents: %.3f\n",
               (double)f, (int)ok, (double)note.number, (int)note.nearest, (int)previous_nearest,
               (unsigned)note.pitch_class, (double)note.cents);
      }
      sweep_failures++;
    }
    previous_nearest = note.nearest;
  }
  test_check(sweep_failures == 0U, "sweep_test failed");
}

/*
  Invalid input must return false and leave *out unchanged.
*/
static void invalid_case(float freq_hz, float a4_hz, const char *description)
{
  const note_result_t initial = { -1234.5f, -999, 200U, 777.0f }; // values no valid result can have
  note_result_t note = initial;
  bool ok = note_from_freq(freq_hz, a4_hz, &note);
  bool changed = note.number != initial.number
              || note.nearest != initial.nearest
              || note.pitch_class != initial.pitch_class
              || note.cents != initial.cents;
  if (ok || changed)
  {
    printf("invalid input, %s: returned %d, out changed: %d\n", description, (int)ok, (int)changed);
    failure_count++;
  }
}

static void invalid_input_test(void)
{
  invalid_case(0.0f,      A4_HZ,    "freq 0");
  invalid_case(-82.41f,   A4_HZ,    "freq negative");
  invalid_case(NAN,       A4_HZ,    "freq NaN"); // pitch gives NaN on invalid frames
  invalid_case(INFINITY,  A4_HZ,    "freq +inf");
  invalid_case(-INFINITY, A4_HZ,    "freq -inf");
  invalid_case(440.0f,    0.0f,     "A4 0");
  invalid_case(440.0f,    -440.0f,  "A4 negative");
  invalid_case(440.0f,    NAN,      "A4 NaN");
  invalid_case(440.0f,    INFINITY, "A4 +inf");
}

typedef struct {
  const char *name;
  float number;
  uint8_t pitch_class;
  float cents;
} fold_case_t;

static const fold_case_t fold_cases[] = {
  { "E2",               40.00f,  4U,    0.0f },
  { "E2 +25",           40.25f,  4U,   25.0f },
  { "E2 -60",           39.40f,  4U,  -60.0f },
  { "E3 -10",           51.90f,  4U,  -10.0f },
  { "E1 +10",           28.10f,  4U,   10.0f },
  { "E6",               88.00f,  4U,    0.0f },
  { "B0",               23.00f, 11U,    0.0f },
  { "C4 -50",           59.50f,  0U,  -50.0f },
  { "E +590",           45.90f,  4U,  590.0f },
  { "E +600",           46.00f,  4U, -600.0f }, // range is [-600, 600)
  { "E +610",           46.10f,  4U, -590.0f },
  // number < class: fmodf keeps the sign of the dividend
  { "number < class",    3.40f,  4U,  -60.0f },
  { "number << class",   2.00f, 11U,  300.0f },
  { "zero",              0.00f,  0U,    0.0f },
};
#define FOLD_CASES_COUNT (sizeof fold_cases / sizeof fold_cases[0])

static void fold_test(void)
{
  for (size_t i = 0; i < FOLD_CASES_COUNT; i++)
  {
    const fold_case_t *c = &fold_cases[i];
    float cents = note_cents_from_class(c->number, c->pitch_class);
    if (!(fabsf(cents - c->cents) <= TOLERANCE_CENTS)) // also fails on NaN
    {
      printf("fold %s: cents actual: %.3f, expected: %.3f\n", c->name, (double)cents, (double)c->cents);
      failure_count++;
    }
  }
}

/*
  note_cents_from_class against the note's own class must give the same cents as note_from_freq.
*/
static void fold_sweep_test(void)
{
  uint32_t sweep_failures = 0U;

  for (float f = SWEEP_F_MIN_HZ; f <= SWEEP_F_MAX_HZ; f *= SWEEP_STEP_RATIO)
  {
    note_result_t note = {};
    if (!note_from_freq(f, A4_HZ, &note))
    {
      continue; // counted in sweep_test
    }
    float folded = note_cents_from_class(note.number, note.pitch_class);
    if (!(fabsf(folded - note.cents) <= TOLERANCE_CENTS))
    {
      if (sweep_failures < SWEEP_MAX_PRINTS)
      {
        printf("fold sweep, %.3f Hz: folded: %.3f, cents: %.3f\n", (double)f, (double)folded, (double)note.cents);
      }
      sweep_failures++;
    }
  }
  test_check(sweep_failures == 0U, "fold_sweep_test failed");
}

static void name_test(void)
{
  static const char *const names[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
  };

  for (uint8_t i = 0U; i < 12U; i++)
  {
    const char *name = note_name(i);
    if (name == NULL || strcmp(name, names[i]) != 0)
    {
      printf("note_name(%u) actual: %s, expected: %s\n", (unsigned)i, name == NULL ? "NULL" : name, names[i]);
      failure_count++;
    }
  }

  test_check(note_name(12U) != NULL && strcmp(note_name(12U), "?") == 0, "note_name(12) is not \"?\"");
  test_check(note_name(255U) != NULL && strcmp(note_name(255U), "?") == 0, "note_name(255) is not \"?\"");
}

int main(void)
{
  freq_test();
  classes_test();
  sweep_test();
  invalid_input_test();
  fold_test();
  fold_sweep_test();
  name_test();

  return failure_count == 0 ? 0 : 1;
}
