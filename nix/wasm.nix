# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# The WebAssembly build: the game compiled with Emscripten into a web page
# (index.html / fa18.html, fa18.js, fa18.wasm),
# following Pingus and SuperTux Origins. Everything is built offline: SDL2
# from its release tarball (a flake input) instead of Emscripten's port,
# which would be downloaded at build time. The original ADF is *not*
# embedded (not redistributable); the shell.html offers an Upload button
# that stores the image in IDBFS for subsequent loads.
#
#   nix build .#fa18-wasm   the site, in result/
#   nix run .#fa18-wasm     serve it locally and open a browser

{ pkgs
, sdlSrc
, sdlVersion
, version
, gitRev
, date
}:

let
  emscripten = pkgs.emscripten;

  # Emscripten writes to its cache; start from the prebuilt one in the
  # store (libc, libc++ ...) instead of building those again.
  setupEmCache = name: ''
    export EM_CACHE="$TMPDIR/emcache-${name}"
    cp -r ${emscripten}/share/emscripten/cache "$EM_CACHE"
    chmod -R u+w "$EM_CACHE"
  '';

  # Static SDL2 for wasm32.
  sdl2Wasm = pkgs.stdenv.mkDerivation {
    pname = "sdl2-wasm";
    version = sdlVersion;
    dontUnpack = true;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = [ emscripten pkgs.cmake pkgs.python3 ];
    env.SDL_SRC = "${sdlSrc}";
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "sdl2"}
      PREFIX=$PWD/prefix bash ${../mk/wasm/scripts/build-sdl2.sh}
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      mkdir -p $out
      cp -a prefix/. $out/
      runHook postInstall
    '';
    dontStrip = true; # the host's strip does not know wasm objects
    meta = {
      description = "Static SDL2 for wasm32-emscripten";
      license = pkgs.lib.licenses.zlib;
      platforms = pkgs.lib.platforms.linux;
    };
  };

  # The game's web page.
  fa18Wasm = pkgs.stdenv.mkDerivation {
    pname = "fa18-wasm";
    inherit version;
    src = pkgs.lib.cleanSource ../.;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = [ emscripten pkgs.cmake pkgs.python3 ];
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "fa18"}
      emcmake cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=$out \
        -DSDL2_ROOT=${sdl2Wasm} \
        -DFA18_RECOMP=OFF \
        -DENABLE_OPENGLES2=ON
      cmake --build build --parallel ''${NIX_BUILD_CORES:-1}
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      cmake --install build
      runHook postInstall
    '';
    dontStrip = true;
    meta = {
      description = "FA-18 Interceptor as a web page (WebAssembly)";
      license = pkgs.lib.licenses.gpl3Plus;
      platforms = pkgs.lib.platforms.linux;
    };
  };

  # `nix run`: serve the site on 127.0.0.1 and open it in a browser.
  serveApp = {
    type = "app";
    program = toString (pkgs.writeShellScript "fa18-wasm-serve" ''
      export PKG=${fa18Wasm}
      export APP_NAME=fa18
      export PATH=${pkgs.python3}/bin:${pkgs.xdg-utils}/bin:$PATH
      exec bash ${../mk/wasm/scripts/serve.sh} "$@"
    '');
    meta.description = "Serve the WebAssembly build of FA-18 Interceptor and open it in a browser";
  };

in {
  inherit sdl2Wasm fa18Wasm serveApp;
}
