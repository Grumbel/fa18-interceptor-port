{
  description = "FA-18 Interceptor – modern C++/SDL2 port (reverse-engineering project)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "fa18-interceptor";
          version = "0.1.0";
          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            python3   # tools/recomp.py generates the recompiled game code
          ];

          buildInputs = with pkgs; [
            SDL2
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          meta = with pkgs.lib; {
            mainProgram = "fa18";
            description = "Modern port of the 1988 Amiga flight simulator F/A-18 Interceptor";
            license = licenses.gpl3Plus;
            platforms = platforms.linux;
          };
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            pkg-config
            SDL2
            # useful for RE
            python3
            xxd
            file
            # m68k objdump for tools/disasm.py
            pkgsCross.m68k.buildPackages.binutils
          ];
          shellHook = ''
            echo "FA-18 Interceptor dev shell"
            echo "  cmake -B build && cmake --build build"
            echo "  ./build/fa18"
          '';
        };
      });
}
