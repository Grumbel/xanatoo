# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# SQLite (amalgamation, generated from nixpkgs' sqlite.src in nix/android.nix)
# as a static ndk-build module for the side store (persisted folds).
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := sqlite3
LOCAL_SRC_FILES := sqlite3.c
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CFLAGS := -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
	-DSQLITE_DEFAULT_MEMSTATUS=0 -Wno-unused-parameter

include $(BUILD_STATIC_LIBRARY)
