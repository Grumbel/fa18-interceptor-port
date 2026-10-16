{
  description = "FA-18 Interceptor – modern C++/SDL2 port (reverse-engineering project)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;

        # Host path to the original ADF.  Read at *evaluation* time only
        # (builtins.getEnv).  It does NOT automatically appear inside a
        # sandboxed build — the value is substituted into the derivation
        # text, and the build must be allowed to open that host path
        # (__noChroot) because the ADF is not in the Nix store.
        #   FA18_DATA=/path/to/game.adf nix flake check --impure
        fa18Data = builtins.getEnv "FA18_DATA";

        mkFa18 = { pname, cmakeExtraFlags ? [ ], extraAttrs ? { } }:
          pkgs.stdenv.mkDerivation ({
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
          } // extraAttrs);

        fa18 = mkFa18 { pname = "fa18-interceptor"; };

        # Recomp build: needs the host ADF to extract the executable once.
        # __noChroot: the ADF path is outside the store.
        fa18Recomp =
          if fa18Data == "" then null
          else mkFa18 {
            pname = "fa18-interceptor-recomp";
            cmakeExtraFlags = [ "-DFA18_ADF=${fa18Data}" ];
            extraAttrs = {
              # Host ADF is not a store input; disable the sandbox so cmake
              # can read FA18_ADF and extract the executable.
              __noChroot = true;
            };
          };

        # Full native translation gate (tests/check_native.sh).
        checkNative =
          if fa18Data == "" then
            pkgs.runCommand "fa18-check-native-skipped" { } ''
              echo "SKIP fa18-check-native: set FA18_DATA to the game ADF (or assets dir)"
              echo "  and re-run with:  FA18_DATA=/path/to/game.adf nix flake check --impure"
              touch "$out"
            ''
          else
            pkgs.stdenv.mkDerivation {
              name = "fa18-check-native";
              src = self;

              # Host ADF path + nested bash scripts with /usr/bin/env shebangs.
              __noChroot = true;

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

              # Rewrite #!/usr/bin/env bash → Nix store bash (sandbox has no /usr/bin/env).
              postPatch = ''
                patchShebangs tests
              '';

              dontConfigure = true;
              dontBuild = true;

              installPhase = ''
                runHook preInstall
                mkdir -p build
                ln -sf ${fa18Recomp}/bin/fa18 build/fa18

                # FA18_DATA is baked in from evaluation-time getEnv (not inherited
                # from the invoking shell into the builder environment).
                export FA18_DATA=${lib.escapeShellArg fa18Data}
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
        } // lib.optionalAttrs (fa18Recomp != null) {
          fa18-recomp = fa18Recomp;
        };

        apps.default = mkApp "default" "--native on";
        apps.native = mkApp "native" "--native on";
        apps.recomp = mkApp "recomp" "--native off";

        # nix flake check [--impure]
        #   checks.build  – package builds (interpreter-only without ADF)
        #   checks.native – tests/check_native.sh (needs FA18_DATA + --impure)
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
            echo "  FA18_DATA=/path/to/game.adf nix flake check --impure"
            echo "  nix run .#native"
            echo "  nix run .#recomp"
          '';
        };
      });
}
