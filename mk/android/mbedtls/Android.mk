# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Mbed TLS (nixpkgs' mbedtls.src, default mbedtls_config.h) as a static
# ndk-build module: the TLS backend for HTTPS / Gemini / Nostr on Android
# (src/tls_mbedtls.cpp).
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := mbedtls
LOCAL_SRC_FILES := $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/library/*.c))
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/library
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
# Entropy: mbedTLS's platform source calls getrandom() blocking, which on
# old Android kernels (Fire OS 5) stalls ~60 s before the pool counts as
# ready. Replace it with mbedtls_hardware_poll() (/dev/urandom) from
# src/tls_mbedtls.cpp; exported so that file sees the same configuration.
ENTROPY_FLAGS := -DMBEDTLS_NO_PLATFORM_ENTROPY -DMBEDTLS_ENTROPY_HARDWARE_ALT
LOCAL_CFLAGS := -O2 -Wno-unused-parameter $(ENTROPY_FLAGS)
LOCAL_EXPORT_CFLAGS := $(ENTROPY_FLAGS)

include $(BUILD_STATIC_LIBRARY)
