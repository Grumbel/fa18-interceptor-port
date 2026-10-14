// Minimal symmetric binary serializer (the same code path saves and loads).
#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace amiga {

struct Ser {
  bool loading = false;
  bool ok = true;
  std::vector<uint8_t> buf;
  size_t pos = 0;

  void bytes(void* p, size_t n) {
    if (loading) {
      if (pos + n > buf.size()) { ok = false; std::memset(p, 0, n); return; }
      std::memcpy(p, buf.data() + pos, n);
      pos += n;
    } else {
      const uint8_t* b = static_cast<const uint8_t*>(p);
      buf.insert(buf.end(), b, b + n);
    }
  }
  template <class T> void pod(T& v) {
    static_assert(std::is_trivially_copyable<T>::value, "pod");
    bytes(&v, sizeof v);
  }
  uint32_t count(size_t n) {
    uint32_t c = uint32_t(n);
    pod(c);
    if (loading && c > (1u << 28)) { ok = false; c = 0; }
    return c;
  }
  template <class T> void vec(std::vector<T>& v) {
    uint32_t c = count(v.size());
    if (loading) v.assign(c, T{});
    if (c) bytes(v.data(), c * sizeof(T));
  }
  template <class T> void dq(std::deque<T>& d) {
    uint32_t c = count(d.size());
    if (loading) {
      d.clear();
      for (uint32_t i = 0; i < c; ++i) { T t{}; pod(t); d.push_back(t); }
    } else {
      for (auto& t : d) pod(t);
    }
  }
  void str(std::string& s) {
    uint32_t c = count(s.size());
    if (loading) s.assign(c, '\0');
    if (c) bytes(s.data(), c);
  }
  template <class K, class V> void map_pod(std::map<K, V>& m) {
    uint32_t c = count(m.size());
    if (loading) {
      m.clear();
      for (uint32_t i = 0; i < c; ++i) { K k{}; V v{}; pod(k); pod(v); m[k] = v; }
    } else {
      for (auto& [k, v] : m) { K kk = k; V vv = v; pod(kk); pod(vv); }
    }
  }
  template <class K> void set_pod(std::set<K>& s) {
    uint32_t c = count(s.size());
    if (loading) {
      s.clear();
      for (uint32_t i = 0; i < c; ++i) { K k{}; pod(k); s.insert(k); }
    } else {
      for (auto& k : s) { K kk = k; pod(kk); }
    }
  }
};

}  // namespace amiga
