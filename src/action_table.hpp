#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <iostream>

// Mirrors the action table at absolute base 0 recovered from the Amiga binary.
// See docs/action_create.md and docs/keyboard.md.

namespace fa18 {

struct ActionSlot {
  std::function<void()> code;  // (slot+0) – callable
  uint32_t field_8  = 0;       // set to $01660000 on create
  uint32_t field_c  = 0;       // set to $003f0000 on create
  uint32_t field_10 = 0;       // set to 1 on create
  uint32_t field_20 = 0;
  bool     active   = false;
};

constexpr int kActionTableSize = 64;
constexpr int kSlotTitle       = 35;  // $8c
constexpr int kSlotProceed     = 36;  // $90

class ActionTable {
public:
  ActionTable() = default;

  // Mirrors overlay create (file 0x4bf14): index×4 into base 0
  void create(int index) {
    if (index < 0 || index >= kActionTableSize) return;
    auto& s = slots_[index];
    s.active   = true;
    s.field_8  = 0x01660000;
    s.field_c  = 0x003f0000;
    s.field_10 = 1;
    s.field_20 = 0;
    s.code     = nullptr;
    std::cout << "[action] create slot " << index << "\n";
  }

  // Early-init path (file 0x9960): create 35 + 36 and customise
  void init_title_slots() {
    create(kSlotTitle);
    create(kSlotProceed);
    // Post-create customisation from file 0x9960:
    slots_[kSlotTitle].field_8   = 0x00b30000;
    slots_[kSlotProceed].field_8 = 0x00b50000;
    slots_[kSlotTitle].field_10  = 0xffffffff;  // -1
    slots_[kSlotProceed].field_10 = 0xffffffff;
  }

  // Dispatcher (file 0x9f7c): invoke if slot non-empty
  void invoke(int index) {
    if (index < 0 || index >= kActionTableSize) return;
    auto& s = slots_[index];
    if (!s.active) return;
    std::cout << "[action] invoke slot " << index << "\n";
    if (s.code) s.code();
  }

  void clear(int index) {
    if (index < 0 || index >= kActionTableSize) return;
    slots_[index] = ActionSlot{};
    std::cout << "[action] clear slot " << index << "\n";
  }

  ActionSlot& operator[](int i) { return slots_[i]; }
  const ActionSlot& operator[](int i) const { return slots_[i]; }

private:
  std::array<ActionSlot, kActionTableSize> slots_{};
};

} // namespace fa18
