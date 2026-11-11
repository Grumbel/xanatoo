// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// R36S only (nix/r36s.nix): the cross compiler is newer than the GCC 9
// libstdc++ of the ArkOS sysroot. Since GCC 13 code that throws out of a
// noexcept function calls __cxa_call_terminate, which libstdc++ 9 lacks.
// Same behaviour as the library's own definition.

#include <cxxabi.h>

#include <exception>

extern "C" void __cxa_call_terminate(void* exc) noexcept {
  if (exc) __cxxabiv1::__cxa_begin_catch(exc);
  std::terminate();
}
