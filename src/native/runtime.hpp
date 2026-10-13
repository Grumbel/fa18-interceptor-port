// Runtime support for the hand-translated ("native") C++ version of the
// game code.
//
// The game's data still lives in the emulated Amiga memory image, so native
// and not-yet-translated (recompiled/interpreted) code can run side by side
// and share state.  Native functions replace the original routine at a given
// address; `fa18 --native off` runs the original code everywhere and
// `fa18 --native verify` runs both versions of every call to a verifiable
// native function and compares the results.
#pragma once

#include "amiga/machine.hpp"

#include <cstdint>
#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>

namespace native {

template <typename T> struct FnArgs;
template <typename R, typename... A> struct FnArgs<R (*)(A...)> {
  using ret = R;
  using type = std::tuple<A...>;
};

// ---- machine access -------------------------------------------------------

extern amiga::Machine* g_machine;
inline amiga::Machine& mach() { return *g_machine; }
inline m68k::Cpu& cpu() { return g_machine->cpu; }

inline uint8_t rd8(uint32_t a) { return g_machine->read8(a); }
inline uint16_t rd16(uint32_t a) { return g_machine->read16(a); }
inline uint32_t rd32(uint32_t a) { return g_machine->read32(a); }
inline void wr8(uint32_t a, uint32_t v) { g_machine->write8(a, uint8_t(v)); }
inline void wr16(uint32_t a, uint32_t v) { g_machine->write16(a, uint16_t(v)); }
inline void wr32(uint32_t a, uint32_t v) { g_machine->write32(a, v); }
inline int8_t rds8(uint32_t a) { return int8_t(rd8(a)); }
inline int16_t rds16(uint32_t a) { return int16_t(rd16(a)); }
inline int32_t rds32(uint32_t a) { return int32_t(rd32(a)); }

// A big-endian variable in the game's memory image, e.g.
//   inline constexpr Var<int16_t> mission_state{0x5aa3a};
//   mission_state = 3; if (mission_state > 2) ...
template <typename T>
struct Var {
  static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4);
  uint32_t addr;

  T get() const {
    if constexpr (sizeof(T) == 1) return T(rd8(addr));
    else if constexpr (sizeof(T) == 2) return T(rd16(addr));
    else return T(rd32(addr));
  }
  void set(T v) const {
    if constexpr (sizeof(T) == 1) wr8(addr, uint32_t(v));
    else if constexpr (sizeof(T) == 2) wr16(addr, uint32_t(v));
    else wr32(addr, uint32_t(v));
  }
  operator T() const { return get(); }
  const Var& operator=(T v) const { set(v); return *this; }
  const Var& operator+=(T v) const { set(T(get() + v)); return *this; }
  const Var& operator-=(T v) const { set(T(get() - v)); return *this; }
  const Var& operator|=(T v) const { set(T(get() | v)); return *this; }
  const Var& operator&=(T v) const { set(T(get() & v)); return *this; }
  const Var& operator^=(T v) const { set(T(get() ^ v)); return *this; }
  T operator++() const { T v = T(get() + 1); set(v); return v; }
  T operator--() const { T v = T(get() - 1); set(v); return v; }
  T operator++(int) const { T v = get(); set(T(v + 1)); return v; }
  T operator--(int) const { T v = get(); set(T(v - 1)); return v; }
  // The variable's address (a Var converts implicitly to its *value*).
  uint32_t ptr() const { return addr; }
  // Element access for arrays starting at addr.
  Var at(int i) const { return Var{uint32_t(addr + uint32_t(i) * sizeof(T))}; }
};

// ---- calling original (68k) code ------------------------------------------

// Call a routine with the C calling convention (args pushed right to left,
// 32-bit each, result in d0). Works whether the routine is native or not.
uint32_t call_c(uint32_t addr, std::initializer_list<uint32_t> args = {});

// Call a routine with register arguments; returns the register file after
// the call (d0-d7 in d[], a0-a6 in a[]).
struct Regs {
  uint32_t d[8] = {};
  uint32_t a[8] = {};
};
Regs call_asm(uint32_t addr, const Regs& in);

// Arguments of the currently executing native C-convention function.
inline uint32_t arg(int i) { return rd32(cpu().a[7] + 4 + uint32_t(i) * 4); }

// ---- registration ---------------------------------------------------------

using Impl = void (*)();

enum class Conv { C, Asm };

struct Def {
  uint32_t addr;
  const char* name;
  Impl fn;
  Conv conv;     // C: stack args, result d0, d2-d7/a2-a6 preserved
  bool verify;   // safe to run twice for --native verify (no OS side effects)
  bool ret_d0;   // C: result in d0 (void functions leave garbage there)
};

// Registers at static-initialisation time.
struct Registrar {
  Registrar(uint32_t addr, const char* name, Impl fn, Conv conv, bool verify, bool ret_d0 = true);
};

const Def* find(uint32_t addr);
void for_each(void (*f)(const Def&));

// Adapter: C-convention native function with N 32-bit stack arguments.
template <auto F>
void c_adapter() {
  using Fn = decltype(F);
  constexpr size_t N = std::tuple_size_v<typename FnArgs<Fn>::type>;
  auto call = [&]<size_t... I>(std::index_sequence<I...>) {
    using Tuple = typename FnArgs<Fn>::type;
    if constexpr (std::is_void_v<typename FnArgs<Fn>::ret>) {
      F(static_cast<std::tuple_element_t<I, Tuple>>(arg(int(I)))...);
      // void: d0 is left as is
    } else {
      using R = typename FnArgs<Fn>::ret;
      uint32_t r = uint32_t(F(static_cast<std::tuple_element_t<I, Tuple>>(arg(int(I)))...));
      // Word/byte results only replace the low part of d0, like the
      // compiled code's move.w/move.b into d0.
      if constexpr (sizeof(R) == 2) cpu().d[0] = (cpu().d[0] & 0xFFFF0000u) | (r & 0xFFFF);
      else if constexpr (sizeof(R) == 1) cpu().d[0] = (cpu().d[0] & 0xFFFFFF00u) | (r & 0xFF);
      else cpu().d[0] = r;
    }
  };
  call(std::make_index_sequence<N>{});
}

}  // namespace native

// Register a C-convention function `fn` (typed parameters, 32-bit stack
// arguments) as the replacement for the original routine at `addr`.
#define NATIVE_C(addr, fn, verify)                                                         \
  static ::native::Registrar native_reg_##fn(                                              \
      addr, #fn, &::native::c_adapter<&fn>, ::native::Conv::C, verify,                     \
      !std::is_void_v<typename ::native::FnArgs<decltype(&fn)>::ret>)

// A game state handler: `void fn()` stored as a code address in a state
// variable (e.g. game_state) and called through it.  Defines fn_addr.
#define NATIVE_STATE(addr, fn)          \
  static void fn();                     \
  constexpr uint32_t fn##_addr = addr;  \
  NATIVE_C(addr, fn, false)
// Register a register-convention routine `void fn()` that reads/writes cpu().
#define NATIVE_ASM(addr, fn, verify) \
  static ::native::Registrar native_reg_##fn(addr, #fn, &fn, ::native::Conv::Asm, verify)
