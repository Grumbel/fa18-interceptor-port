# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# libmain.so: the game, linked against the prebuilt SDL2 (see build-apk.sh,
# which stages src/amiga, src/m68k, src/native, src/recomp, src/main.cpp).
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := main

# Staged by build-apk.sh under jni/src/
LOCAL_SRC_FILES := \
  main.cpp \
  stats_overlay.cpp \
  recomp/stub.cpp \
  $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/amiga/*.cpp)) \
  $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/m68k/*.cpp)) \
  $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/native/*.cpp))

LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/../SDL/include
LOCAL_CPPFLAGS += -std=c++20 -Wno-psabi -Wall -Wextra -O2 \
	-DFA18_OPENGLES2=1 \
	-DFA18_VERSION=\"$(FA18_VERSION)\"
LOCAL_CPP_FEATURES := exceptions rtti
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -llog -landroid -lGLESv2 -lEGL

include $(BUILD_SHARED_LIBRARY)
