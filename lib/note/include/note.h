#ifndef NOTE_H
#define NOTE_H

#include <stdint.h>

// see https://en.wikipedia.org/wiki/MIDI_tuning_standard

#define NOTE_A4_DEFAULT_HZ 440.0f

typedef struct
{
  float number;        // n = 69 + 12 * log2(f / A4)
  int32_t nearest;     // round(n)
  uint8_t pitch_class; // nearest % 12: the note regardless of octave, 0-11, C = 0, B = 11
  float cents;         // distance from the nearest note of this class
} note_result_t;

// returns true when a new result was written; *out is not changed when returned false
// returns false and doesn't change the state if freq_hz or a4_hz is not finite or <= 0
// out must not be null
bool note_from_freq(float freq_hz, float a4_hz, note_result_t *out);

// distance from number to the nearest note of this class; range [-600, +600) - two halves of the 12 notes
float note_cents_from_class(float number, uint8_t pitch_class);

// "C", "C#", ..., "A#", "B". Sharps only; anything above 11 returns "?".
const char *note_name(uint8_t pitch_class);

#endif /* NOTE_H */
