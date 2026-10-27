# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# libmain.so: xanatoo, linked against the prebuilt SDL2 and the static
# FreeType module (see build-apk.sh, which puts src/, include/ and external/
# next to this file). First port: no OpenSSL clients, no FontConfig (font
# files are looked up in /system/fonts), no SQLite side store.
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := main
LOCAL_SRC_FILES := \
	$(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/src/*.cpp)) \
	external/imgui/imgui.cpp \
	external/imgui/imgui_draw.cpp \
	external/imgui/imgui_tables.cpp \
	external/imgui/imgui_widgets.cpp \
	external/imgui/backends/imgui_impl_sdl2.cpp \
	external/imgui/backends/imgui_impl_sdlrenderer2.cpp
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/include \
	$(LOCAL_PATH)/external \
	$(LOCAL_PATH)/external/imgui \
	$(LOCAL_PATH)/external/imgui/backends \
	$(LOCAL_PATH)/../SDL/include/SDL2
LOCAL_CPPFLAGS += -std=c++20 -Wno-psabi -Wall -Wextra -Wno-unused-parameter \
	-O2 \
	-DXANATOO_GEMINI=0 -DXANATOO_NOSTR=0 -DXANATOO_REDDIT=0 \
	-DXANATOO_LEMMY=0 -DXANATOO_FONTCONFIG=0 -DXANATOO_SQLITE=0
LOCAL_CPP_FEATURES := exceptions rtti
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_STATIC_LIBRARIES := freetype
LOCAL_LDLIBS := -llog -landroid

include $(BUILD_SHARED_LIBRARY)
