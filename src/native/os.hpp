// AmigaOS calls as made by the game: through its own amiga.lib stubs (so
// behaviour is identical to the original code paths).  Pointers are Amiga
// addresses.
#pragma once

#include "native/runtime.hpp"

namespace native::os {

inline uint32_t u(int32_t v) { return uint32_t(v); }

// dos.library
inline int32_t Open(uint32_t name, int32_t mode) { return int32_t(call_c(0x68f44, {name, u(mode)})); }
inline int32_t Close(int32_t fh) { return int32_t(call_c(0x68f60, {u(fh)})); }
inline int32_t Read(int32_t fh, uint32_t buf, int32_t len) { return int32_t(call_c(0x68f74, {u(fh), buf, u(len)})); }
inline int32_t Write(int32_t fh, uint32_t buf, int32_t len) { return int32_t(call_c(0x68f90, {u(fh), buf, u(len)})); }
inline int32_t Seek(int32_t fh, int32_t pos, int32_t mode) { return int32_t(call_c(0x68fcc, {u(fh), u(pos), u(mode)})); }
inline int32_t Lock(uint32_t name, int32_t mode) { return int32_t(call_c(0x69018, {name, u(mode)})); }
inline void UnLock(int32_t lock) { call_c(0x69034, {u(lock)}); }
inline int32_t Examine(int32_t lock, uint32_t fib) { return int32_t(call_c(0x69048, {u(lock), fib})); }
inline int32_t Info(int32_t lock, uint32_t info) { return int32_t(call_c(0x69064, {u(lock), info})); }
inline int32_t IoErr() { return int32_t(call_c(0x69080)); }
inline int32_t DeleteFile(uint32_t name) { return int32_t(call_c(0x68fe8, {name})); }
inline int32_t Rename(uint32_t from, uint32_t to) { return int32_t(call_c(0x68ffc, {from, to})); }
constexpr int32_t MODE_OLDFILE = 1005, MODE_NEWFILE = 1006;

// exec.library
inline uint32_t AllocMem(uint32_t size, uint32_t req) { return call_c(0x690c8, {size, req}); }
inline void FreeMem(uint32_t p, uint32_t size) { call_c(0x690e0, {p, size}); }
inline uint32_t AvailMem(uint32_t req) { return call_c(0x690f8, {req}); }
inline uint32_t FindTask(uint32_t name) { return call_c(0x6910c, {name}); }
inline int32_t SetTaskPri(uint32_t task, int32_t pri) { return int32_t(call_c(0x69120, {task, u(pri)})); }
inline uint32_t GetMsg(uint32_t port) { return call_c(0x691a0, {port}); }
inline uint32_t WaitPort(uint32_t port) { return call_c(0x691b4, {port}); }
inline void CloseLibrary(uint32_t lib) { call_c(0x691c8, {lib}); }
inline int32_t OpenDevice(uint32_t name, uint32_t unit, uint32_t io, uint32_t flags) { return int32_t(call_c(0x691dc, {name, unit, io, flags})); }
inline void CloseDevice(uint32_t io) { call_c(0x691fc, {io}); }
inline int32_t DoIO(uint32_t io) { return int32_t(call_c(0x69210, {io})); }
inline void SendIO(uint32_t io) { call_c(0x69224, {io}); }
inline uint32_t OpenLibrary(uint32_t name, uint32_t ver) { return call_c(0x69238, {name, ver}); }
inline void AddIntServer(uint32_t n, uint32_t is) { call_c(0x69098, {n, is}); }
inline void RemIntServer(uint32_t n, uint32_t is) { call_c(0x690b0, {n, is}); }
inline uint32_t CreatePort(uint32_t name, int32_t pri) { return call_c(0x6927c, {name, u(pri)}); }
inline void DeletePort(uint32_t port) { call_c(0x69324, {port}); }
inline uint32_t CreateExtIO(uint32_t port, uint32_t size) { return call_c(0x693b8, {port, size}); }
inline void DeleteExtIO(uint32_t io) { call_c(0x69400, {io}); }
constexpr uint32_t MEMF_CHIP = 2, MEMF_CLEAR = 0x10000;

// graphics.library
inline void LoadRGB4(uint32_t vp, uint32_t colors, int32_t n) { call_c(0x6944c, {vp, colors, u(n)}); }
inline void InitRastPort(uint32_t rp) { call_c(0x69468, {rp}); }
inline void InitVPort(uint32_t vp) { call_c(0x6947c, {vp}); }
inline void MrgCop(uint32_t view) { call_c(0x69490, {view}); }
inline void MakeVPort(uint32_t view, uint32_t vp) { call_c(0x694a4, {view, vp}); }
inline void LoadView(uint32_t view) { call_c(0x694bc, {view}); }
inline void WaitBlit() { call_c(0x694d0); }
inline void InitView(uint32_t view) { call_c(0x694e0, {view}); }
inline void InitBitMap(uint32_t bm, int32_t depth, int32_t w, int32_t h) { call_c(0x694f4, {bm, u(depth), u(w), u(h)}); }
inline void WaitBOVP(uint32_t vp) { call_c(0x69514, {vp}); }
inline void FreeSprite(int32_t n) { call_c(0x69528, {u(n)}); }
inline void OwnBlitter() { call_c(0x6953c); }
inline void DisownBlitter() { call_c(0x6954c); }
inline void FreeRaster(uint32_t p, int32_t w, int32_t h) { call_c(0x6955c, {p, u(w), u(h)}); }
inline uint32_t GetColorMap(int32_t n) { return call_c(0x69578, {u(n)}); }

// intuition.library
inline int32_t CloseWorkBench() { return int32_t(call_c(0x695a4)); }

}  // namespace native::os
