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
          ];

          buildInputs = with pkgs; [
            SDL2
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          # Assets are copied by the CMake POST_BUILD rule; also install them
          postInstall = ''
            mkdir -p $out/share/fa18-interceptor
            cp -r assets $out/share/fa18-interceptor/
            # Convenience wrapper that runs from the share dir so relative assets work
            mkdir -p $out/bin
            mv $out/bin/fa18 $out/bin/fa18-bin
            cat > $out/bin/fa18 <<EOF_WRAP
#!/bin/sh
cd $out/share/fa18-interceptor
exec $out/bin/fa18-bin "\$@"
EOF_WRAP
            chmod +x $out/bin/fa18
          '';

          meta = with pkgs.lib; {
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
            # optional later: ghidra, vasm, etc.
          ];
          shellHook = ''
            echo "FA-18 Interceptor dev shell"
            echo "  cmake -B build && cmake --build build"
            echo "  ./build/fa18"
          '';
        };
      });
}
