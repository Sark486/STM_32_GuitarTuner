#include "note.h"
#include <math.h>
#include <stdint.h>

#define NOTE_A4_MIDI_NUMBER 69
#define NOTE_CLASSES 12


bool note_from_freq(float freq_hz, float a4_hz, note_result_t *out)
{
  if (!isfinite(freq_hz) || !(freq_hz > 0.0f) ||
      !isfinite(a4_hz)   || !(a4_hz > 0.0f))
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
  if (r < 0.0f) {
    r += NOTE_CLASSES;  // fmodf keeps the dividend's sign; move it into [0, 12)
  }
  return (r - half) * 100.0f;
}


const char *note_name(uint8_t pitch_class)
{
  switch (pitch_class) {
    case 0:  return "C";
    case 1:  return "C#";
    case 2:  return "D";
    case 3:  return "D#";
    case 4:  return "E";
    case 5:  return "F";
    case 6:  return "F#";
    case 7:  return "G";
    case 8:  return "G#";
    case 9:  return "A";
    case 10: return "A#";
    case 11: return "B";
    default: return "?";
  }
}
