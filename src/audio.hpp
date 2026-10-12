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

  // Sample block mirrors live format (docs/audio_channels.md).
  struct SampleBlock {
    const int8_t* data = nullptr;  // +0  AUDxLC
    int32_t length_bytes = 0;      // +4  → AUDxLEN = bytes/2
    int16_t period = 0x7C;         // +8  clamped ≤ $7C
    int16_t volume = 0;            // +12 bits 0–5 (0–63)
    int32_t countdown = -1;        // +16 −1 = sticky
    int32_t reload = 0;            // +20
    SampleBlock* next = nullptr;   // +32 chain
  };

  // $00C4ED00 — program AUDx from sample block and enable DMA.
  void play(int idx, SampleBlock* block) {
    if (idx < 0 || idx > 3) return;
    if (!block || !block->data) {
      stop(idx);
      return;
    }
    int16_t per = block->period;
    if (per > 0x7C) per = 0x7C;
    int16_t vol = static_cast<int16_t>(block->volume & 0x3F);
    enable(idx);
    std::cout << "[audio] play ch " << idx
              << " len=" << block->length_bytes
              << " per=" << per << " vol=" << vol << "\n";
    // SDL backend: queue 8-bit signed PCM at ~3.58MHz/(per*2)
  }

  void stop(int idx) {
    if (idx < 0 || idx > 3) return;
    ch_[idx].active = false;
    holders_[idx] = nullptr;
    std::cout << "[audio] stop ch " << idx << "\n";
  }

  // $00C16684 — queue a sample-block template onto a channel and fire INTREQ.
  // Mirrors: template table $00C06460 → holder $00C07488+4*ch → $00C4EBD8.
  void queue(int idx, SampleBlock* block) {
    if (idx < 0 || idx > 3) return;
    holders_[idx] = block;
    request(idx);
    if (block)
      play(idx, block);
    else
      stop(idx);
  }

  // $00C16660 — clear channel holder (silence on next interrupt).
  void clear_queue(int idx) {
    if (idx < 0 || idx > 3) return;
    holders_[idx] = nullptr;
    request(idx);
    stop(idx);
  }

  SampleBlock* holder(int idx) const {
    if (idx < 0 || idx > 3) return nullptr;
    return holders_[idx];
  }

  AudioChannel& operator[](int i) { return ch_[i]; }

private:
  std::array<AudioChannel, 4> ch_;
  std::array<SampleBlock*, 4> holders_{};
};

}  // namespace fa18
