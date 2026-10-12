#pragma once
#include <cstdint>
#include <array>
#include <iostream>

// Mirrors live audio channel descriptors at $00C07478 (docs/audio_channels.md).
namespace fa18 {

struct AudioChannel {
  uint16_t dmacon_set = 0;  // +10  SET|AUDn
  uint16_t dmacon_bit = 0;  // +12  channel bit
  uint16_t intreq_set = 0;  // +14  SET|AUDn int
  uint16_t int_bit    = 0;  // +16  interrupt bit
  bool     active     = false;
};

inline std::array<AudioChannel, 4> make_default_channels() {
  // Live patterns from $00C07498 family
  return {{
    {0x8201, 0x0001, 0x8080, 0x0080, false},
    {0x8202, 0x0002, 0x8100, 0x0100, false},
    {0x8204, 0x0004, 0x8200, 0x0200, false},
    {0x8208, 0x0008, 0x8400, 0x0400, false},
  }};
}

class AudioSystem {
public:
  AudioSystem() : ch_(make_default_channels()) {}

  // $00C4EB60 — enable DMA bit for channel
  void enable(int idx) {
    if (idx < 0 || idx > 3) return;
    ch_[idx].active = true;
    std::cout << "[audio] enable ch " << idx
              << " DMACON=$" << std::hex << ch_[idx].dmacon_set << std::dec << "\n";
  }

  // $00C4EBD8 — poke INTREQ for channel
  void request(int idx) {
    if (idx < 0 || idx > 3) return;
    std::cout << "[audio] INTREQ ch " << idx
              << "=$" << std::hex << ch_[idx].intreq_set << std::dec << "\n";
  }

  // $00C4ED00 outline — play sample (backend TBD)
  void play(int idx, const int8_t* /*sample*/, int len, int period, int vol) {
    if (idx < 0 || idx > 3) return;
    enable(idx);
    std::cout << "[audio] play ch " << idx
              << " len=" << len << " per=" << period << " vol=" << vol << "\n";
  }

  void stop(int idx) {
    if (idx < 0 || idx > 3) return;
    ch_[idx].active = false;
    std::cout << "[audio] stop ch " << idx << "\n";
  }

  AudioChannel& operator[](int i) { return ch_[i]; }

private:
  std::array<AudioChannel, 4> ch_;
};

}  // namespace fa18
