{
  description = "FA-18 Interceptor – modern C++/SDL2 port (reverse-engineering project)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;  # requireFile ADF is marked unfree
        };
        lib = pkgs.lib;

        # Original game disk — not redistributable, not in the repo.
        # Store path names cannot contain spaces; content hash is of the
        # TOSEC 901120-byte ADF (flat).  Add it once:
        #
        #   cp "FA-18 Interceptor (1988)(Electronic Arts).adf" fa18-interceptor.adf
        #   nix-store --add-fixed sha256 fa18-interceptor.adf
        fa18Adf = pkgs.requireFile {
          name = "fa18-interceptor.adf";
          # nix hash path --mode flat 'FA-18 Interceptor (1988)(Electronic Arts).adf'
          hash = "sha256-PdSPiYVWG+ItkCOT3juu4ALWQlX6OCaDX8sgmHXGJUA=";
          message = ''
            The original FA-18 Interceptor ADF is required for the native
            verification gate and for a recompiled build, but it is not part
            of this repository.

            1. Obtain the TOSEC image, e.g. "FA-18 Interceptor (1988)(Electronic Arts).adf"

            2. Add it to the Nix store under the fixed name (content hash):

               nix store add-path --name fa18-interceptor.adf --mode flat "FA-18 Interceptor (1988)(Electronic Arts).adf"

            3. Re-run the build / check.
          '';
        };

        mkFa18 = { pname, cmakeExtraFlags ? [ ] }: pkgs.stdenv.mkDerivation {
          inherit pname;
          version = "0.1.0";
          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            python3
          ];

          buildInputs = with pkgs; [
            SDL2
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ] ++ cmakeExtraFlags;

          meta = with lib; {
            mainProgram = "fa18";
            description = "Modern port of the 1988 Amiga flight simulator F/A-18 Interceptor";
            license = licenses.gpl3Plus;
            platforms = platforms.linux;
          };
        };

        fa18 = mkFa18 { pname = "fa18-interceptor"; };

        fa18Recomp = mkFa18 {
          pname = "fa18-interceptor-recomp";
          cmakeExtraFlags = [ "-DFA18_ADF=${fa18Adf}" ];
        };

        checkNative = pkgs.stdenv.mkDerivation {
          name = "fa18-check-native";
          src = self;

          nativeBuildInputs = [
            fa18Recomp
            pkgs.python3
            pkgs.coreutils
            pkgs.bash
            pkgs.gnugrep
            pkgs.gnused
            pkgs.gawk
            pkgs.findutils
            pkgs.util-linux
            pkgs.diffutils
          ];
          buildInputs = [ pkgs.SDL2 ];

          # #!/usr/bin/env bash → store bash (sandbox has no /usr/bin/env).
          postPatch = ''
            patchShebangs tests
          '';

          dontConfigure = true;
          dontBuild = true;

          installPhase = ''
            runHook preInstall
            mkdir -p build
            ln -sf ${fa18Recomp}/bin/fa18 build/fa18

            # ADF is a real store input (requireFile); sandbox can read it.
            export FA18_DATA=${fa18Adf}
            export FA18_SKIP_BUILD=1
            export SDL_VIDEODRIVER=dummy
            export SDL_AUDIODRIVER=dummy

            echo "== native gate (FA18_DATA=$FA18_DATA)"
            mkdir -p "$TMPDIR/gate"
            if ! bash tests/check_native.sh "$TMPDIR/gate"; then
              echo "FAIL: tests/check_native.sh"
              exit 1
            fi
            mkdir -p "$out"
            echo PASS > "$out/result"
            if [ -f "$TMPDIR/gate/compare.txt" ]; then
              cp "$TMPDIR/gate/compare.txt" "$out/compare.txt" || true
            fi
            runHook postInstall
          '';

          meta.description = "Native C++ translation gate (tests/check_native.sh)";
        };

        mkApp = name: extraArgs: {
          type = "app";
          program = "${pkgs.writeShellScript "fa18-${name}" ''
            exec ${fa18}/bin/fa18 ${extraArgs} "$@"
          ''}";
        };
      in
      {
        packages = {
          default = fa18;
          fa18 = fa18;
          fa18-recomp = fa18Recomp;
        };

        apps.default = mkApp "default" "--native on";
        apps.native = mkApp "native" "--native on";
        apps.recomp = mkApp "recomp" "--native off";

        # nix flake check
        #   checks.build  – package (interpreter-only; no ADF needed)
        #   checks.native – tests/check_native.sh (ADF via requireFile in the store)
        checks = {
          build = fa18;
          native = checkNative;
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            pkg-config
            SDL2
            python3
            xxd
            file
            pkgsCross.m68k.buildPackages.binutils
          ];
          shellHook = ''
            echo "FA-18 Interceptor dev shell"
            echo "  cmake -B build && cmake --build build"
            echo "  ./build/fa18 /path/to/game.adf"
            echo "  # native gate — add ADF to the store first (see flake requireFile):"
            echo "  cp /path/to/game.adf fa18-interceptor.adf && nix-store --add-fixed sha256 fa18-interceptor.adf"
            echo "  nix build .#checks.x86_64-linux.native"
            echo "  nix run .#native"
            echo "  nix run .#recomp"
          '';
        };
      });
}
