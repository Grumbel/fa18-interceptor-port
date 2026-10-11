# FA-18 Interceptor – Modern Port

Reverse-engineering and port of the classic 1988 Amiga flight simulator
**F/A-18 Interceptor** (Electronic Arts / Intellisoft) to C++ / SDL2.

## Build (Nix)

```bash
nix develop          # enter dev shell with SDL2 + cmake
cmake -B build
cmake --build build
./build/fa18
```

Or build the package:

```bash
nix build
./result/bin/fa18
```

## Build (plain CMake)

Requires SDL2 development packages:

```bash
cmake -B build
cmake --build build
./build/fa18
```

## Controls (current skeleton)

- **SPACE / ENTER** – advance high-level state
- **BACKSPACE** – previous state
- **ESC** – quit

## Status

See `TODO.md` and `docs/` for reverse-engineering notes, binary structure,
WHDLoad offsets, and the recovered high-level flow.

## License

New code: GPLv3+.  
Original game assets remain under their original copyright; this is an
educational / clean-room style reverse-engineering effort.
