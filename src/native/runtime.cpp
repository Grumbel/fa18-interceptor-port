#include "native/runtime.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace native {

amiga::Machine* g_machine = nullptr;

namespace {
std::map<uint32_t, Def>& registry() {
  static std::map<uint32_t, Def> r;
  return r;
}
}  // namespace

Registrar::Registrar(uint32_t addr, const char* name, Impl fn, Conv conv, bool verify, bool ret_d0,
                     int nargs) {
  registry()[addr] = Def{addr, name, fn, conv, verify, ret_d0, nargs};
}

const Def* find(uint32_t addr) {
  auto it = registry().find(addr);
  return it == registry().end() ? nullptr : &it->second;
}

void for_each(void (*f)(const Def&)) {
  for (auto& [a, d] : registry()) f(d);
}

// Debug aid: FA18_POISON=all|addr,addr (hex) fills d0-d7/a0-a6 with a
// pattern (FA18_POISON_SEED) before calling the routine, to find original
// routines that read registers they were not given.
static void poison(uint32_t addr, m68k::Cpu& c) {
  static const char* list = getenv("FA18_POISON");
  if (!list) return;
  if (std::string(list) != "all") {
    char key[16];
    snprintf(key, sizeof key, ",%x,", addr);
    if ((std::string(",") + list + ",").find(key) == std::string::npos) return;
  }
  static uint32_t seed = getenv("FA18_POISON_SEED") ? uint32_t(strtoul(getenv("FA18_POISON_SEED"), nullptr, 0)) : 0xDEAD0000u;
  for (int i = 0; i < 8; ++i) c.d[i] = seed + uint32_t(i);
  for (int i = 0; i < 7; ++i) c.a[i] = (seed + 0x100u + uint32_t(i) * 4) & 0x7FFFFC;
}

uint32_t call_c(uint32_t addr, std::initializer_list<uint32_t> args) {
  // Like a jsr from compiled C code: the callee's register changes stay
  // visible (compiled code does not restore what a callee clobbers, and
  // some original routines depend on such leftovers); only the arguments
  // are popped again.
  m68k::Cpu& c = cpu();
  uint32_t sp = c.a[7];
  poison(addr, c);
  const uint32_t* a = args.begin();
  for (size_t i = args.size(); i-- > 0;) c.push32(a[i]);
  g_machine->call_sub(addr);
  c.a[7] = sp;
  return c.d[0];
}

Regs call_asm(uint32_t addr, const Regs& in) {
  m68k::Cpu& c = cpu();
  uint32_t sp = c.a[7];
  for (int i = 0; i < 8; ++i) c.d[i] = in.d[i];
  for (int i = 0; i < 7; ++i) c.a[i] = in.a[i];
  g_machine->call_sub(addr);
  c.a[7] = sp;
  Regs out;
  for (int i = 0; i < 8; ++i) { out.d[i] = c.d[i]; out.a[i] = c.a[i]; }
  return out;
}

}  // namespace native
