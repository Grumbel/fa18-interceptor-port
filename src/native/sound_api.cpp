// Game-side sound API: sample loading and the per-effect trigger functions
// (engine, jet-blast, gun, missile, warning tones, UI beeps).
// Original: hunk 0 $29668-$2a2f6 (compiled C) on top of the sound driver
// (hunks 75-77 / 80, still original code).
//
// Sample objects are 64-byte records reached through two tables:
//   $65bac[i]  pointer to sample object i (i = sample number)
//   $65128[c]  pointer to the object playing on channel c
// Object fields (as used here): +8 pitch/period (16.16), +12 volume (16.16),
// +16 / +20 loop/priority flags, +24 / +28 pitch/volume slope, +32 link to
// the paired (second channel) object, +44 start flag, +48 envelope table
// pointer, +52 envelope position, +56 / +60 slope divisors.
#include "native/globals.hpp"
#include "native/orig.hpp"

namespace native {
using namespace g;

namespace {
constexpr uint32_t sample_tab = 0x65bac;   // 416684
constexpr uint32_t channel_tab = 0x65128;  // 413992
constexpr Var<uint8_t> snd_flags_lo{0x5ae52};   // loaded-effect bits (low byte)
constexpr Var<uint8_t> snd_flags_hi{0x5ae53};
constexpr Var<int8_t> ui_sound_state{0x5aacf};
inline uint32_t obj_of(uint32_t var) { return rd32(var); }
inline void set_field(uint32_t obj, uint32_t off, uint32_t v) { wr32(obj + off, v); }
inline uint32_t field(uint32_t obj, uint32_t off) { return rd32(obj + off); }
// $6846c: signed 32-bit divide (0 for a zero divisor).
inline int32_t sdiv(int32_t n, int32_t d) {
  if (d == 0 || n == 0) return 0;
  return int32_t(int64_t(n) / int64_t(d));
}
// Link bit twiddle used by the paired-effect set-up: add `delta` to the
// object's first long and rebase its second long (top bit kept).
inline void rebase(uint32_t obj, uint32_t src, uint32_t delta) {
  wr32(obj, rd32(obj) + delta);
  uint32_t v = ((rd32(src + 4) & 0x7fffffffu) - delta) | 0x80000000u;
  wr32(obj + 4, v);
}
}  // namespace

// $29c84: start sample `sample` on `channel` with volume `vol` (0-63).
static void play_sample(int32_t sample, int32_t channel, int32_t vol) {
  uint32_t obj = rd32(sample_tab + uint32_t(sample) * 4);
  if (obj == 0) return;
  sub_29c60(channel);
  obj = rd32(sample_tab + uint32_t(sample) * 4);
  wr32(obj + 12, uint32_t(vol) << 16);
  wr32(channel_tab + uint32_t(channel) * 4, obj);
  sub_6529c(channel);
}
NATIVE_C(0x29c84, play_sample, false);

// $29cee: start-up/menu beep on channels 0/1 (muted once `ui_sound_state` is set).
static void play_ui_sound(int32_t vol) {
  if (ui_sound_state != 0) return;
  if (snd_flags_hi & 0x80) {
    sub_215fe();             // stop all channels
    play_sample(13, 0, 63);
    play_sample(14, 1, 63);
    ui_sound_state = 2;
  } else if (snd_flags_lo & 0x04) {
    play_sample(35, 0, vol);
    play_sample(36, 1, vol);
    ui_sound_state = 2;
  } else {
    sub_215fe();
  }
}
NATIVE_C(0x29cee, play_ui_sound, false);

// $29668: load the UI sample pair (35/36) and set up their objects.
static void load_ui_samples() {
  if (sub_658ae(0x2a5dc, 35) == 0) return;
  if (sub_65934(35, 36) == 0) return;
  uint32_t a = rd32(0x65c38), b = rd32(0x65c3c);
  wr32(b + 16, uint32_t(-1));
  wr32(a + 16, uint32_t(-1));
  wr32(a + 8, 11730944);
  wr32(b + 8, 11862016);
  snd_flags_lo = uint8_t(snd_flags_lo | 0x04);
}
NATIVE_C(0x29668, load_ui_samples, false);

// $296c2: load the cockpit effect samples 13-34 (seven source files, each
// with derived copies), initialise their objects, or release them on failure.
static void load_effect_samples() {
  int32_t missing = 0;
  sub_658ae(0x2a5ed, 13); sub_65934(13, 14);
  sub_658ae(0x2a5fe, 15); sub_65934(15, 16);
  sub_658ae(0x2a60f, 17); sub_65934(17, 18);
  sub_658ae(0x2a61f, 19); sub_65934(19, 20);
  sub_65934(17, 21); sub_65934(17, 22);
  sub_65934(19, 23); sub_65934(19, 24);
  sub_658ae(0x2a62f, 25); sub_65934(25, 26);
  sub_658ae(0x2a63f, 27); sub_65934(27, 28);
  sub_65934(25, 29); sub_65934(25, 30);
  sub_65934(27, 31); sub_65934(27, 32);
  sub_658ae(0x2a64f, 33); sub_65934(33, 34);
  for (int i = 13; i < 35; ++i)
    if (rd32(sample_tab + uint32_t(i) * 4) == 0) ++missing;
  if (missing != 0) {
    for (int i = 13; i < 35; ++i) sub_65af6(uint32_t(i));
    return;
  }
  for (int i = 13; i < 35; ++i) {
    uint32_t o = rd32(sample_tab + uint32_t(i) * 4);
    wr32(o + 20, 0);
    wr32(o + 16, 0);
    // link to the object two entries further on (the second channel)
    wr32(o + 32, rd32(sample_tab + 8 + uint32_t(i) * 4));
  }
  wr32(rd32(0x65be4) + 16, 13);
  wr32(rd32(0x65be0) + 16, 13);
  wr32(rd32(0x65bec) + 16, 1);
  wr32(rd32(0x65be8) + 16, 1);
  // Loop count 2 on the middle group, 3 on the last pair.
  constexpr uint32_t two[] = {0x65c24, 0x65c20, 0x65c14, 0x65c10, 0x65c04, 0x65c00, 0x65bf4, 0x65bf0};
  for (uint32_t v : two) { uint32_t o = rd32(v); wr32(o + 20, 2); wr32(o + 16, 2); }
  uint32_t a0 = rd32(0x65c30), a1 = rd32(0x65c34);
  wr32(a1 + 20, 3); wr32(a1 + 16, 3);
  wr32(a0 + 20, 3); wr32(a0 + 16, 3);
  wr32(a0 + 32, rd32(0x65bf0));
  wr32(a1 + 32, rd32(0x65bf4));
  snd_flags_hi = uint8_t(snd_flags_hi | 0x80);
}
NATIVE_C(0x296c2, load_effect_samples, false);

// $299d2: load the remaining effects (engine, afterburner, gun, warnings,
// missile, ...), each guarded by its availability bit in $5ae52/$5ae53.
static void load_flight_effects() {
  bool ok = false;
  if (sub_658ae(0x2a65f, 2) != 0) ok = true;
  else if (sub_658ae(0x2a670, 2) != 0) ok = true;
  if (ok && sub_65934(2, 3) != 0) {
    uint32_t a = rd32(0x65bb4);
    wr32(a + 16, uint32_t(-1));
    a = rd32(0x65bb8);
    wr32(a + 16, uint32_t(-1));
    rebase(a, a, 880);
    snd_flags_hi = uint8_t(snd_flags_hi | 0x02);
  }
  if (sub_6578c(32, 4) != 0) {
    uint32_t a = rd32(0x65bbc);
    sub_65e28(rd32(a), rd32(a + 4));
    a = rd32(0x65bbc);
    wr32(a + 48, 417624);
    wr32(a + 16, uint32_t(-1));
  }
  if (sub_6578c(2048, 6) != 0) {
    uint32_t a = rd32(0x65bc4);
    sub_65f06(rd32(a), rd32(a + 4));
    a = rd32(0x65bc4);
    wr32(a + 16, uint32_t(-1));
    wr32(a + 48, 417720);
    snd_flags_lo = uint8_t(snd_flags_lo | 0x01);
  }
  if (sub_658ae(0x2a682, 5) != 0) snd_flags_hi = uint8_t(snd_flags_hi | 0x04);
  if (rd32(0x65bc4) != 0 && sub_65934(6, 10) != 0) {
    uint32_t a = rd32(0x65bd4);
    wr32(a + 48, 417872);
    wr32(a + 8, 9830400);
    snd_flags_hi = uint8_t(snd_flags_hi | 0x20);
  }
  if (sub_658ae(0x2a693, 11) != 0) {
    wr32(rd32(0x65bd8) + 16, uint32_t(-1));
    snd_flags_hi = uint8_t(snd_flags_hi | 0x40);
  }
  if (rd32(0x65bb4) != 0 && sub_65934(2, 12) != 0) {
    wr32(rd32(0x65bdc) + 48, 417808);
    snd_flags_lo = uint8_t(snd_flags_lo | 0x02);
  }
  if (sub_658ae(0x2a6a4, 0) != 0 && sub_65934(0, 1) != 0) {
    wr32(rd32(0x65bac) + 16, uint32_t(-1));
    uint32_t b = rd32(0x65bb0);
    wr32(b + 16, uint32_t(-1));
    wr32(b + 4, uint32_t(-2147478068));
    snd_flags_hi = uint8_t(snd_flags_hi | 0x01);
  }
  if (sub_658ae(0x2a6b5, 8) != 0 && sub_65934(8, 9) != 0) {
    uint32_t a = rd32(0x65bd0);
    wr32(a + 16, uint32_t(-1));
    wr32(rd32(0x65bcc) + 16, uint32_t(-1));
    rebase(a, rd32(0x65bb8), 1938);
    snd_flags_hi = uint8_t(snd_flags_hi | 0x10);
  }
}
NATIVE_C(0x299d2, load_flight_effects, false);

// ---- effect triggers ---------------------------------------------------------

// $29e4e: jet engine pair on channels 0/1 (samples 2/3); `pitch` is the
// engine setting (16.16 base), `vol` the volume.
static void engine_sound_b(int32_t pitch, int32_t vol) {
  if (!(snd_flags_hi & 0x02) || rd32(0x65bb8) == 0) return;
  uint32_t p = rd32(0x65bb4), q = rd32(0x65bb8);
  wr32(q + 28, 0); wr32(p + 28, 0);
  wr32(q + 24, 0); wr32(p + 24, 0);
  wr32(p + 8, uint32_t(pitch) << 16);
  wr32(q + 8, uint32_t(pitch + 2) << 16);
  play_sample(2, 0, vol);
  play_sample(3, 1, vol);
}
NATIVE_C(0x29e4e, engine_sound_b, false);

// $29dba: same for the alternative engine sound set (samples 0/1) when
// available, otherwise falls back to engine_sound_b with a quarter pitch.
static void engine_sound_a(int32_t pitch, int32_t vol) {
  if (snd_flags_hi & 0x01) {
    if (rd32(0x65bb0) == 0) return;
    uint32_t p = rd32(0x65bb0), q = rd32(0x65bac);
    wr32(p + 28, 0); wr32(q + 28, 0);
    wr32(p + 24, 0); wr32(q + 24, 0);
    wr32(q + 8, uint32_t(pitch) << 16);
    wr32(p + 8, uint32_t(pitch + 2) << 16);
    play_sample(0, 0, vol);
    play_sample(1, 1, vol);
  } else {
    engine_sound_b(pitch, vol >> 2);
  }
}
NATIVE_C(0x29dba, engine_sound_a, false);

// $29f02: slide the engine pitch/volume to targets over `steps` frames.
static void engine_slide_b(int32_t pitch, int32_t vol, int32_t steps) {
  if (!(snd_flags_hi & 0x02) || rd32(0x6512c) == 0) return;
  uint32_t p = rd32(0x6512c), q = rd32(0x65128);
  int32_t d = sdiv(int32_t(uint32_t(pitch) << 16) - int32_t(rd32(q + 8)), steps);
  wr32(p + 24, uint32_t(d)); wr32(q + 24, uint32_t(d));
  wr32(p + 56, uint32_t(steps)); wr32(q + 56, uint32_t(steps));
  d = sdiv(int32_t(uint32_t(vol) << 16) - int32_t(rd32(q + 12)), steps);
  wr32(p + 28, uint32_t(d)); wr32(q + 28, uint32_t(d));
  wr32(p + 60, uint32_t(steps)); wr32(q + 60, uint32_t(steps));
}
NATIVE_C(0x29f02, engine_slide_b, false);

// $29ec6: engine slide, quarter volume for the second sound set.
static void engine_slide(int32_t pitch, int32_t vol, int32_t steps) {
  if (snd_flags_hi & 0x01) engine_slide_b(pitch, vol, steps);
  else engine_slide_b(pitch, vol >> 2, steps);
}
NATIVE_C(0x29ec6, engine_slide, false);

// $29fa2: wind/turbine loop (samples 8/9) with random pitch; stops the
// pair if the effect is not available.
static void turbine_sound(int32_t vol) {
  if (snd_flags_hi & 0x10) {
    if (rd32(0x65bd0) == 0) return;
    uint32_t o = rd32(0x65bcc);
    wr32(o + 8, (sub_65ed2(6) + 360) << 16);
    o = rd32(0x65bd0);
    wr32(o + 8, (sub_65ed2(6) + 360) << 16);
    play_sample(8, 0, vol);
    play_sample(9, 1, vol);
  } else {
    sub_29c60(0);
    sub_29c60(1);
  }
}
NATIVE_C(0x29fa2, turbine_sound, false);

// $2a04a: cannon / weapon effect: nine parameters for the synthesised
// envelope tables at $65f5c..$65fa4, then start sample 4 on channel 3.
static void weapon_sound(int32_t p0, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5,
                         int32_t p6, int32_t p7, int32_t p8) {
  if (rd32(0x65bbc) == 0) return;
  sub_29c60(3);
  wr32(0x65f5c, uint32_t(p7));
  wr32(0x65f64, uint32_t(p6));
  wr32(0x65f6c, uint32_t(p0) << 16);
  wr32(0x65f74, uint32_t(p1) << 16);
  wr32(0x65f7c, uint32_t(p2));
  wr32(0x65f84, uint32_t(p3) << 16);
  wr32(0x65f8c, uint32_t(p4) << 16);
  wr32(0x65f94, uint32_t(p5));
  wr32(0x65fa4, uint32_t(p8));
  uint32_t o = rd32(0x65bbc);
  wr32(o + 52, 0);
  wr32(o + 44, 1);
  play_sample(4, 3, 0);
}
NATIVE_C(0x2a04a, weapon_sound, false);

// $2a0e4: afterburner roar (sample 6 on channel 2) sliding from `level`
// down; without the effect set the game's flame-out state is flagged.
static void afterburner_sound(int32_t level, int32_t steps) {
  if (snd_flags_lo & 0x01) {
    if (rd32(0x65bc4) == 0) return;
    sub_29c60(2);
    int32_t v = int32_t(uint32_t(level) << 16);
    wr32(0x65fbc, uint32_t(v));
    wr32(0x65fc4, uint32_t(sdiv(-v, steps)));
    wr32(0x65fcc, uint32_t(steps));
    uint32_t o = rd32(0x65bc4);
    wr32(o + 52, 0);
    uint32_t r = sub_65ed2(11);
    wr32(o + 8, (r * 4 + 8990) << 16);
    wr32(rd32(0x65bc4) + 44, 1);
    play_sample(6, 2, 0);
  } else {
    wr8(0x5aba8, uint8_t(-6));
    wr8(0x5aa8f, 2);
  }
}
NATIVE_C(0x2a0e4, afterburner_sound, false);

// $2a194: missile launch whoosh (sample 5 on channel 2) when the player
// is in the current view.
static void missile_sound(int32_t vol) {
  if (!(snd_flags_hi & 0x04)) return;
  if (rd16(0x5acae) != rd16(0x5abd6)) return;
  uint32_t o = rd32(0x65bc0);
  if (o == 0) return;
  wr32(o + 8, 20316160);
  wr32(o + 24, uint32_t(-131072));
  wr32(o + 16, 1);
  play_sample(5, 2, vol);
}
NATIVE_C(0x2a194, missile_sound, false);

// $2a1ee: warning tone (sample 11 on channel 2), restarted from its envelope.
static void warning_sound(int32_t vol) {
  if (!(snd_flags_hi & 0x40)) return;
  if (rd16(0x5acae) != rd16(0x5abd6)) return;
  uint32_t o = rd32(0x65bd8);
  if (o == 0) return;
  sub_29c60(2);
  o = rd32(0x65bd8);
  wr32(o + 44, 0);
  wr32(o + 48, 417760);
  wr32(o + 52, 0);
  wr32(o + 44, 1);
  play_sample(11, 2, vol);
}
NATIVE_C(0x2a1ee, warning_sound, false);

// $2a254: silence channel 2.
static void stop_channel_2() { sub_29c60(2); }
NATIVE_C(0x2a254, stop_channel_2, false);

// $2a260: rotor/flap sound (sample 12 on channel 2) with a fading level.
static void flap_sound(int32_t level) {
  if (!(snd_flags_lo & 0x02) || rd32(0x65bdc) == 0) return;
  sub_29c60(2);
  wr32(0x6602c, uint32_t(level) << 16);
  int32_t n = -int32_t(uint32_t(level - 1) << 16);
  wr32(0x66034, uint32_t(sdiv(n, 60)));
  uint32_t o = rd32(0x65bdc);
  wr32(o + 52, 0);
  uint32_t r = sub_65ed2(3);
  wr32(o + 8, (r + 140) << 16);
  wr32(rd32(0x65bdc) + 44, 1);
  play_sample(12, 2, 0);
}
NATIVE_C(0x2a260, flap_sound, false);

}  // namespace native
