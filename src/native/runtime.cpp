#include "native/runtime.hpp"

#include <map>

namespace native {

amiga::Machine* g_machine = nullptr;

namespace {
std::map<uint32_t, Def>& registry() {
  static std::map<uint32_t, Def> r;
  return r;
}
}  // namespace

Registrar::Registrar(uint32_t addr, const char* name, Impl fn, Conv conv, bool verify) {
  registry()[addr] = Def{addr, name, fn, conv, verify};
}

const Def* find(uint32_t addr) {
  auto it = registry().find(addr);
  return it == registry().end() ? nullptr : &it->second;
}

void for_each(void (*f)(const Def&)) {
  for (auto& [a, d] : registry()) f(d);
}

uint32_t call_c(uint32_t addr, std::initializer_list<uint32_t> args) {
  m68k::Cpu& c = cpu();
  m68k::Cpu saved = c;
  const uint32_t* a = args.begin();
  for (size_t i = args.size(); i-- > 0;) c.push32(a[i]);
  g_machine->call_sub(addr);
  uint32_t ret = c.d[0];
  int64_t cycles = c.cycles;
  c = saved;
  c.cycles = cycles;
  return ret;
}

Regs call_asm(uint32_t addr, const Regs& in) {
  m68k::Cpu& c = cpu();
  m68k::Cpu saved = c;
  for (int i = 0; i < 8; ++i) c.d[i] = in.d[i];
  for (int i = 0; i < 7; ++i) c.a[i] = in.a[i];
  g_machine->call_sub(addr);
  Regs out;
  for (int i = 0; i < 8; ++i) { out.d[i] = c.d[i]; out.a[i] = c.a[i]; }
  int64_t cycles = c.cycles;
  c = saved;
  c.cycles = cycles;
  return out;
}

}  // namespace native
