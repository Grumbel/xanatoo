# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# thumtoo (external/thumtoo) as a static module, compiled by ndk-build with
# the app's C++ runtime, over the prebuilt C libraries from
# nix/android-imagelibs.nix (deps/<abi>/: libimagedeps.a = libvips, glib,
# libarchive, codecs). build-apk.sh puts thumtoo's src/, include/ and
# external/ next to this file and generates gen/thumtoo/version.hpp.
# No MuPDF / DjVu / unarr / curl / Tesseract / D-Bus; SQLite is the app's
# sqlite3 module.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := imagedeps
LOCAL_SRC_FILES := deps/$(TARGET_ARCH_ABI)/lib/libimagedeps.a
LOCAL_EXPORT_C_INCLUDES := \
	$(LOCAL_PATH)/deps/$(TARGET_ARCH_ABI)/include \
	$(LOCAL_PATH)/deps/$(TARGET_ARCH_ABI)/include/glib-2.0
LOCAL_EXPORT_LDLIBS := -llog -lm -ldl
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := thumtoo
LOCAL_SRC_FILES := \
	$(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/src/*.cpp)) \
	$(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/src/lod/*.cpp)) \
	external/basic_handsum_decode_impl.c
LOCAL_SRC_FILES := $(filter-out src/archive_unarr.cpp src/pdf_mupdf.cpp,$(LOCAL_SRC_FILES))
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/gen $(LOCAL_PATH)/src
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/gen
LOCAL_EXPORT_CFLAGS := -DTHUMTOO_HAVE_DBUS=0
LOCAL_CFLAGS := -O2 -DTHUMTOO_HAVE_DBUS=0 -Wno-unused-parameter
LOCAL_CPPFLAGS := -std=c++20 -Wno-psabi
LOCAL_CPP_FEATURES := exceptions rtti
LOCAL_STATIC_LIBRARIES := imagedeps sqlite3
include $(BUILD_STATIC_LIBRARY)
