#include "note.h"
#include <math.h>
#include <stdint.h>

#define NOTE_A4_MIDI_NUMBER 69
#define NOTE_CLASSES        12

bool note_from_freq(float freq_hz, float a4_hz, note_result_t *out)
{
  if (!isfinite(freq_hz) || !(freq_hz > 0.0f) || !isfinite(a4_hz) || !(a4_hz > 0.0f))
  {
    return false;
  }
  out->number = NOTE_A4_MIDI_NUMBER + NOTE_CLASSES * log2f(freq_hz / a4_hz);
  out->nearest = (int32_t)lroundf(out->number);
  out->pitch_class = (uint8_t)(((out->nearest % NOTE_CLASSES) + NOTE_CLASSES) % NOTE_CLASSES);
  out->cents = note_cents_from_class(out->number, out->pitch_class);
  return true;
}

float note_cents_from_class(float number, uint8_t pitch_class)
{
  const float half = NOTE_CLASSES / 2.0f;
  float r = fmodf((number - (float)pitch_class) + half, (float)NOTE_CLASSES);
  if (r < 0.0f)
  {
    r += NOTE_CLASSES; // fmodf keeps the dividend's sign; move it into [0, 12)
  }
  return (r - half) * 100.0f;
}

const char *note_name(uint8_t pitch_class)
{
  static const char *const names[NOTE_CLASSES] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return pitch_class < NOTE_CLASSES ? names[pitch_class] : "?";
}
