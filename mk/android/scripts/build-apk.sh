#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the APK: the game's sources as libmain.so with ndk-build against the
# prebuilt SDL2, packaged with SDL's Java side, aligned and signed (after
# Pingus's pipeline, without data or extra libraries).
#
# Env: ANDROID_HOME, BUILD_TOOLS_VERSION, PACKAGE_PLATFORM, TARGET_ABIS,
#      APP_NAME, APP_DIR (manifest, res/, jni/Android.mk), GAME_SRC_DIR (the
#      repository: src/ and include/), APPLICATION_MK, TOP_ANDROID_MK,
#      SDL_PREBUILT_MK, SDL_ANDROID_LIBS, KEYSTORE, GAME_VERSION,
#      VERSION_CODE
set -euo pipefail

# shellcheck source=ndk.sh
. "$(dirname "$0")/ndk.sh"
NDK="$(resolve_ndk)"
echo "==> NDK=$NDK"
BT="$ANDROID_HOME/build-tools/$BUILD_TOOLS_VERSION"
PACKAGE_JAR="$ANDROID_HOME/platforms/android-$PACKAGE_PLATFORM/android.jar"

mkdir -p src/jni/src src/jni/SDL/include
cp "$APPLICATION_MK" src/jni/Application.mk
cp "$TOP_ANDROID_MK" src/jni/Android.mk
cp "$APP_DIR/jni/Android.mk" src/jni/src/Android.mk
cp -r "$APP_DIR/res" src/res
sed -e "s/@VERSION_NAME@/$GAME_VERSION/" -e "s/@VERSION_CODE@/$VERSION_CODE/" \
  "$APP_DIR/AndroidManifest.xml" > src/AndroidManifest.xml

# The game: stage the multi-directory tree next to the module's Android.mk.
# Interpreter-only (recomp stub); ADF is supplied at runtime by the user.
cp "$GAME_SRC_DIR"/src/main.cpp src/jni/src/
cp "$GAME_SRC_DIR"/src/stats_overlay.cpp src/jni/src/
cp "$GAME_SRC_DIR"/src/stats_overlay.hpp src/jni/src/
cp "$GAME_SRC_DIR"/src/font6x10.hpp src/jni/src/
mkdir -p src/jni/src/amiga src/jni/src/m68k src/jni/src/native src/jni/src/recomp
cp "$GAME_SRC_DIR"/src/amiga/*.{cpp,hpp} src/jni/src/amiga/
cp "$GAME_SRC_DIR"/src/m68k/*.{cpp,hpp} src/jni/src/m68k/
# .inc headers (ops_body.inc) are included by native sources.
cp "$GAME_SRC_DIR"/src/native/*.{cpp,hpp,inc} src/jni/src/native/ 2>/dev/null || true
cp "$GAME_SRC_DIR"/src/recomp/*.{cpp,hpp} src/jni/src/recomp/

# SDL2, prebuilt; headers land under jni/SDL/include/SDL2/ (see Android.mk).
cp "$SDL_PREBUILT_MK" src/jni/SDL/Android.mk
cp -r "$SDL_ANDROID_LIBS/include" src/jni/SDL/include/SDL2
chmod -R u+rwX src

"$NDK/ndk-build" \
  NDK_PROJECT_PATH="$PWD/src" \
  APP_BUILD_SCRIPT="$PWD/src/jni/Android.mk" \
  NDK_APPLICATION_MK="$PWD/src/jni/Application.mk" \
  FA18_VERSION="$GAME_VERSION" \
  -j"${NIX_BUILD_CORES:-$(nproc)}"

mkdir -p out
"$BT/aapt" package -f -M src/AndroidManifest.xml -S src/res -I "$PACKAGE_JAR" -F out/base.apk
cp "$SDL_ANDROID_LIBS/dex/classes.dex" out/classes.dex
for abi in $TARGET_ABIS; do
  mkdir -p "out/lib/$abi"
  cp src/libs/"$abi"/*.so "out/lib/$abi/"
done
( cd out && "$BT/aapt" add base.apk classes.dex && zip -r base.apk lib )

"$BT/zipalign" -f 4 out/base.apk out/aligned.apk
cp "$KEYSTORE" debug.keystore
"$BT/apksigner" sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --out "out/$APP_NAME.apk" out/aligned.apk
"$BT/aapt" dump badging "out/$APP_NAME.apk" | head -5
