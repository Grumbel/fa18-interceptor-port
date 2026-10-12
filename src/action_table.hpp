#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <iostream>

// Mirrors the action table (static base 0 → live $00C06460).
// Create $00C4F010 / clear $00C4F3FE. See docs/action_create.md.

namespace fa18 {

struct ActionSlot {
  // +0: resource id / chip offset (NOT a function pointer).
  // Dispatch is via side tables $00C07478/$00C07488 + $00C4EBD8.
  uint32_t field_0  = 0;
  uint32_t field_4  = 0;       // flags; bit 31 gates sub-block free
  uint32_t field_8  = 0;       // type/mode ($01660000 on create)
  uint32_t field_c  = 0;       // param ($003f0000 on create; also arg<<1)
  uint32_t field_10 = 0;       // state/refcount (1 on create)
  uint32_t field_14 = 0;       // written to INTREQ by $00C4EBD8
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
    s.field_0  = 0;
    s.field_4  = 0;
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

  // Dispatcher $00C16684: side tables + $00C4EBD8 (not jsr through +0).
  void invoke(int index, uint32_t arg = 0) {
    if (index < 0 || index >= kActionTableSize) return;
    auto& s = slots_[index];
    if (!s.active) return;
    s.field_c = arg << 1;  // object+12 = arg<<1
    std::cout << "[action] invoke slot " << index
              << " arg=" << arg << " field0=$" << std::hex << s.field_0
              << std::dec << "\n";
    // Real work is $00C4EBD8 family (INTREQ / vector call).
  }

  // Mirrors clear body at live $00C4F3FE (docs/action_create.md):
  // index×4 into table base $00C06460, FreeMem object (+ optional sub-block),
  // then clr.l slot.
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
