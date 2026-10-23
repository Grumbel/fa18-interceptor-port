{
  description = "FA-18 Interceptor – modern C++/SDL2 port (reverse-engineering project)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # SDL2's official prebuilt MinGW package for the Windows builds
    # (nix/windows.nix), as Pingus / Kurvenrausch use it.
    SDL2-win32.url = "github:grumnix/SDL2-win32";
    SDL2-win32.inputs.nixpkgs.follows = "nixpkgs";
    SDL2-win32.inputs.flake-utils.follows = "flake-utils";

    # SDL2 sources for the WebAssembly and Android builds.
    sdl2-src = {
      url = "https://github.com/libsdl-org/SDL/releases/download/release-2.30.3/SDL2-2.30.3.tar.gz";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, SDL2-win32, sdl2-src }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;  # requireFile ADF is marked unfree
        };
        lib = pkgs.lib;

        version = "0.1.0";
        gitRev = "${self.shortRev or self.dirtyShortRev or "dirty"}";
        lastModified = self.lastModifiedDate or "19700101000000";
        date = "${builtins.substring 0 4 lastModified}-${builtins.substring 4 2 lastModified}-${builtins.substring 6 2 lastModified}";

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
          inherit version;
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
            if [ -f "$TMPDIR/gate/interp.txt" ]; then
              cp "$TMPDIR/gate/interp.txt" "$out/interp.txt" || true
            fi
            runHook postInstall
          '';

          meta.description = "Native C++ translation gate (tests/check_native.sh)";
        };

        # ── Cross / port packages (Linux hosts only) ─────────────────────
        isLinux = lib.hasSuffix "-linux" system;

        wasm = if isLinux then import ./nix/wasm.nix {
          inherit pkgs version gitRev date;
          sdlSrc = sdl2-src;
          sdlVersion = "2.30.3";
        } else null;

        windows = if isLinux then import ./nix/windows.nix {
          inherit pkgs version date;
          sdl2Win64 = SDL2-win32.packages.${system}.SDL2-win64;
          sdl2Win32 = SDL2-win32.packages.${system}.SDL2-win32;
        } else null;

        r36s = if isLinux then import ./nix/r36s.nix {
          inherit pkgs version date;
        } else null;

        androidPkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
          config.android_sdk.accept_license = true;
        };
        android = if isLinux then import ./nix/android.nix {
          pkgs = androidPkgs;
          sdlSrc = sdl2-src;
          sdlVersion = "2.30.3";
          buildToolsVersion = "30.0.3";
          packagePlatform = "22";
          compilePlatform = "33";
          targetAbis = [ "armeabi-v7a" "arm64-v8a" "x86_64" ];
          androidSdk = (androidPkgs.androidenv.composeAndroidPackages {
            platformVersions = [ "22" "33" ];
            buildToolsVersions = [ "30.0.3" ];
            includeNDK = true;
            ndkVersion = "29.0.14206865";
            includeEmulator = false;
            includeSources = false;
          }).androidsdk;
          inherit version;
          versionCode = self.revCount or 1;
        } else null;

        mkApp = name: pkg: extraArgs: {
          type = "app";
          program = "${pkgs.writeShellScript "fa18-${name}" ''
            exec ${pkg}/bin/fa18 ${extraArgs} "$@"
          ''}";
        };
      in
      {
        packages = {
          default = fa18;
          fa18 = fa18;
          fa18-recomp = fa18Recomp;
        } // lib.optionalAttrs (wasm != null) {
          fa18-wasm = wasm.fa18Wasm;
        } // lib.optionalAttrs (windows != null) {
          fa18-win64 = windows.win64;
          fa18-win64-zip = windows.win64Zip;
          fa18-win32 = windows.win32;
          fa18-win32-zip = windows.win32Zip;
        } // lib.optionalAttrs (r36s != null) {
          fa18-r36s = r36s.r36s;
          fa18-r36s-portmaster = r36s.portmaster;
          fa18-r36s-portmaster-zip = r36s.portmasterZip;
        } // lib.optionalAttrs (android != null) {
          fa18-android = android.apk;
        };

        # default: no ADF needed at build time (interpreter + native code);
        # native/recomp: recompiled build (ADF via requireFile).
        apps = {
          default = mkApp "default" fa18 "--native on";
          native = mkApp "native" fa18Recomp "--native on";
          recomp = mkApp "recomp" fa18Recomp "--native off";
          interpret = mkApp "interpret" fa18 "--native off --interpret";
        } // lib.optionalAttrs (wasm != null) {
          fa18-wasm = wasm.serveApp;
        } // lib.optionalAttrs (android != null) {
          fa18-android-install = android.installApp;
        };

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
            echo "  # ports (Linux host):"
            echo "  nix build .#fa18-wasm       # WebAssembly site"
            echo "  nix run   .#fa18-wasm       # serve + open browser"
            echo "  nix build .#fa18-win64      # Windows 64-bit folder"
            echo "  nix build .#fa18-r36s       # R36S / ArkOS aarch64"
            echo "  nix build .#fa18-android    # APK (needs Android SDK)"
          '';
        };
      });
}
