# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# Windows builds, cross-compiled with MinGW (pkgsCross), as in Pingus: SDL2
# is the official prebuilt MinGW package (the SDL2-win32 flake). Each build
# is a folder with fa18.exe and the DLLs it needs, and a zip of it.
#
#   nix build .#fa18-win64       the folder
#   nix build .#fa18-win64-zip   fa18-<version>-win64.zip
#   (and -win32 for 32-bit Windows)

{ pkgs
, sdl2Win64
, sdl2Win32
, version
, date
}:

let
  lib = pkgs.lib;

  mkGame = { crossPkgs, sdl2, arch }:
    crossPkgs.stdenv.mkDerivation {
      pname = "fa18-${arch}-build";
      inherit version;
      src = lib.cleanSource ../.;
      nativeBuildInputs = [ pkgs.cmake ];
      buildInputs = [ sdl2 crossPkgs.windows.mcfgthreads ];
      cmakeFlags = [
        "-DCMAKE_BUILD_TYPE=Release"
        "-DBUILD_TESTING=OFF"
        "-DFA18_RECOMP=OFF"
        "-DSDL2_DIR=${sdl2}/lib/cmake/SDL2"
      ];
    };

  # The folder to ship: the program, its DLLs, the licence and a readme.
  mkFolder = { crossPkgs, sdl2, arch }:
    let game = mkGame { inherit crossPkgs sdl2 arch; };
    in pkgs.runCommand "fa18-${arch}-${version}" { } ''
      mkdir -p $out
      cp ${game}/bin/fa18.exe $out/
      cp ${sdl2}/bin/SDL2.dll $out/
      cp ${crossPkgs.windows.mcfgthreads}/bin/libmcfgthread-[0-9]*.dll $out/ # the one the program imports
      cp ${../LICENSES/GPL-3.0-or-later.txt} $out/COPYING.txt
      cat > $out/README.txt <<'EOF'
F/A-18 Interceptor (modern port) ${version}

A C++/SDL2 port of the 1988 Amiga flight simulator.

Place a TOSEC copy of "FA-18 Interceptor (1988)(Electronic Arts).adf"
next to fa18.exe (or pass its path as the first argument), then start
fa18.exe. Keyboard, mouse and gamepads work as on the Amiga original.

The ADF is not redistributed with this build — you must supply your
own legal copy.

Licence: GPL-3.0-or-later (COPYING.txt).
EOF
      sed -i 's/^      //; s/$/\r/' $out/README.txt
    '';

  mkZip = { folder, arch }:
    pkgs.runCommand "fa18-${version}-${arch}.zip" { nativeBuildInputs = [ pkgs.zip ]; } ''
      mkdir -p $out
      work=$(mktemp -d)
      cp -r --no-preserve=mode ${folder} "$work/fa18-${version}-${arch}"
      cd "$work"
      zip -r -9 $out/fa18-${version}-${arch}.zip fa18-${version}-${arch}
    '';

  win64 = mkFolder { crossPkgs = pkgs.pkgsCross.mingwW64; sdl2 = sdl2Win64; arch = "win64"; };
  win32 = mkFolder { crossPkgs = pkgs.pkgsCross.mingw32; sdl2 = sdl2Win32; arch = "win32"; };

in {
  inherit win64 win32;
  win64Zip = mkZip { folder = win64; arch = "win64"; };
  win32Zip = mkZip { folder = win32; arch = "win32"; };
}
