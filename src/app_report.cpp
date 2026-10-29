// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Startup report (gamepkgstd LOGGING.md): what this run actually got, so a
// log answers "what does it run on" instead of leaving it to assumptions.
// Library versions at runtime next to the built-against ones and the file
// that was really loaded, video driver, renderer and OpenGL strings,
// controllers and where their mappings came from, resolved directories.
// One "topic: details" line each, on stderr. Choices made later while
// loading (fonts, UI font, icons, style, the ImGui render path) are logged
// where they are made, in the same format.

#include "xanatoo/app.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/version.hpp"

#if XANATOO_HTTPS
#include "xanatoo/tls.hpp"
#endif
#if XANATOO_THUMTOO
#include "xanatoo/image_backend.hpp"
#include <thumtoo/version.hpp>
#endif

#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <imgui.h>
#include <sqlite3.h>

#if XANATOO_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/utsname.h>
extern char** environ;
#endif
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace xanatoo::app {

namespace {

std::string version3(int a, int b, int c) {
  return std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c);
}

// "runtime (built against built)", or just one when they agree.
std::string versions(const std::string& runtime, const std::string& built) {
  return runtime == built ? runtime : runtime + " (built against " + built + ")";
}

std::string join(const std::vector<std::string>& v, const char* sep) {
  std::string out;
  for (const std::string& s : v) out += (out.empty() ? "" : sep) + s;
  return out;
}

std::string compiler() {
#if defined(__clang__)
  return "Clang " __clang_version__;
#elif defined(__GNUC__)
  return "GCC " __VERSION__;
#elif defined(_MSC_VER)
  return "MSVC " + std::to_string(_MSC_VER);
#else
  return "unknown compiler";
#endif
}

const char* arch() {
#if defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "aarch64";
#elif defined(__arm__)
  return "arm";
#elif defined(__i386__) || defined(_M_IX86)
  return "x86";
#elif defined(__wasm32__)
  return "wasm32";
#else
  return "unknown arch";
#endif
}

const char* build_type() {
#ifdef XANATOO_BUILD_TYPE
  if (XANATOO_BUILD_TYPE[0] != '\0') return XANATOO_BUILD_TYPE;
#endif
#ifdef NDEBUG
  return "NDEBUG";
#else
  return "assertions on";
#endif
}

std::string features() {
  std::vector<std::string> on, off;
  auto add = [&](const char* name, bool enabled) {
    (enabled ? on : off).push_back(name);
  };
  add("web", XANATOO_WEB);
  add("wikipedia", XANATOO_WIKIPEDIA);
  add("hn", XANATOO_HN);
  add("reddit", XANATOO_REDDIT);
  add("lemmy", XANATOO_LEMMY);
  add("nostr", XANATOO_NOSTR);
  add("gemini", XANATOO_GEMINI);
  add("feeds", XANATOO_FEEDS);
  add("ebooks", XANATOO_EBOOKS);
  add("thumtoo", XANATOO_THUMTOO);
  add("fontconfig", XANATOO_FONTCONFIG);
  add("sqlite", XANATOO_SQLITE);
  std::string s = join(on, " ");
  if (!off.empty()) s += "; off: " + join(off, " ");
  return s;
}

std::string system_line() {
  std::string s = SDL_GetPlatform();
#if !defined(_WIN32)
  utsname u{};
  if (uname(&u) == 0) {
    if (s != u.sysname) s += std::string(" ") + u.sysname;
    s += std::string(" ") + u.release + " " + u.machine;
  }
#endif
#ifdef __ANDROID__
  s += ", API " + std::to_string(SDL_GetAndroidSDKVersion());
  char model[PROP_VALUE_MAX] = {};
  if (__system_property_get("ro.product.model", model) > 0)
    s += std::string(", ") + model;
#endif
  s += ", " + std::to_string(SDL_GetCPUCount()) + " CPUs";
  std::vector<std::string> simd;
  if (SDL_HasSSE2()) simd.push_back("sse2");
  if (SDL_HasAVX2()) simd.push_back("avx2");
  if (SDL_HasNEON()) simd.push_back("neon");
  if (!simd.empty()) s += " (" + join(simd, " ") + ")";
  s += ", " + std::to_string(SDL_GetSystemRAM()) + " MB RAM";
  return s;
}

// Variables that change what SDL or the dynamic linker picks. SDL_* and the
// loader ones with values (cut), xanatoo's own by name only: they may hold
// API secrets.
std::vector<std::string> env_lines() {
  std::vector<std::string> out;
#if !defined(_WIN32)
  for (char** e = environ; e && *e; ++e) {
    const std::string kv = *e;
    const std::string name = kv.substr(0, kv.find('='));
    if (name.rfind("XANATOO_", 0) == 0) {
      out.push_back(name + " (set)");
    } else if (name.rfind("SDL_", 0) == 0 || name == "LD_PRELOAD" ||
               name == "LD_LIBRARY_PATH") {
      out.push_back(kv.size() > 100 ? kv.substr(0, 100) + "…" : kv);
    }
  }
  std::sort(out.begin(), out.end());
#endif
  return out;
}

// Shared objects mapped into this process (Linux / Android): which file a
// library really came from, e.g. which of several installed SDL2 builds.
std::vector<std::string> mapped_libraries() {
  std::vector<std::string> libs;
#if defined(__linux__)
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    const size_t slash = line.find('/');
    if (slash == std::string::npos) continue;
    std::string path = line.substr(slash);
    if (path.find(".so") == std::string::npos) continue;
    if (std::find(libs.begin(), libs.end(), path) == libs.end())
      libs.push_back(std::move(path));
  }
#endif
  return libs;
}

std::string mapped(const std::vector<std::string>& libs, const char* name) {
  for (const std::string& p : libs) {
    const std::string base = p.substr(p.rfind('/') + 1);
    if (base.rfind(name, 0) == 0) return p;
  }
  return {};
}

std::string with_path(std::string s, const std::string& path) {
  return path.empty() ? s : s + ", " + path;
}

std::string freetype_runtime() {
  FT_Library ft = nullptr;
  if (FT_Init_FreeType(&ft) != 0) return "?";
  FT_Int a = 0, b = 0, c = 0;
  FT_Library_Version(ft, &a, &b, &c);
  FT_Done_FreeType(ft);
  return version3(a, b, c);
}

std::string video_line(App& app) {
  std::string s = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?";
  const int di = SDL_GetWindowDisplayIndex(app.window);
  SDL_DisplayMode mode{};
  if (di >= 0 && SDL_GetCurrentDisplayMode(di, &mode) == 0) {
    s += ", display " + std::to_string(di) + "/" +
         std::to_string(SDL_GetNumVideoDisplays());
    if (const char* name = SDL_GetDisplayName(di)) s += std::string(" \"") + name + "\"";
    s += " " + std::to_string(mode.w) + "x" + std::to_string(mode.h) + " @ " +
         std::to_string(mode.refresh_rate) + " Hz " +
         SDL_GetPixelFormatName(mode.format);
    float ddpi = 0.f;
    if (SDL_GetDisplayDPI(di, &ddpi, nullptr, nullptr) == 0)
      s += ", " + std::to_string(static_cast<int>(ddpi + 0.5f)) + " dpi";
  }
  int ww = 0, wh = 0, dw = 0, dh = 0;
  SDL_GetWindowSize(app.window, &ww, &wh);
  SDL_GetRendererOutputSize(app.sdl, &dw, &dh);
  const Uint32 flags = SDL_GetWindowFlags(app.window);
  const char* mode_name =
      (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP
          ? "fullscreen desktop"
          : (flags & SDL_WINDOW_FULLSCREEN) ? "fullscreen" : "windowed";
  s += ", window " + std::to_string(ww) + "x" + std::to_string(wh) +
       " (drawable " + std::to_string(dw) + "x" + std::to_string(dh) + ", " +
       mode_name + "), ui scale " + std::to_string(app.ui_scale).substr(0, 4);
  return s;
}

std::string renderer_line(App& app) {
  SDL_RendererInfo ri{};
  if (SDL_GetRendererInfo(app.sdl, &ri) != 0) return std::string("? ") + SDL_GetError();
  std::string s = ri.name ? ri.name : "?";
  std::vector<std::string> fl;
  if (ri.flags & SDL_RENDERER_SOFTWARE) fl.push_back("software");
  if (ri.flags & SDL_RENDERER_ACCELERATED) fl.push_back("accelerated");
  if (ri.flags & SDL_RENDERER_PRESENTVSYNC) fl.push_back("vsync");
  if (SDL_RenderTargetSupported(app.sdl)) fl.push_back("render targets");
  s += " (" + join(fl, ", ") + "), max texture " +
       std::to_string(ri.max_texture_width) + "x" +
       std::to_string(ri.max_texture_height);
  std::vector<std::string> fmts;
  for (Uint32 i = 0; i < ri.num_texture_formats; ++i) {
    std::string f = SDL_GetPixelFormatName(ri.texture_formats[i]);
    if (f.rfind("SDL_PIXELFORMAT_", 0) == 0) f = f.substr(16);
    fmts.push_back(f);
  }
  if (!fmts.empty()) s += ", formats " + join(fmts, " ");
  std::vector<std::string> drivers;
  for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
    SDL_RendererInfo di{};
    if (SDL_GetRenderDriverInfo(i, &di) == 0 && di.name) drivers.push_back(di.name);
  }
  s += "; available " + join(drivers, " ");
  return s;
}

// GL strings of the renderer's context (opengl / opengles2 renderers),
// queried through SDL so no GL headers or libraries are needed.
std::string opengl_line() {
  if (!SDL_GL_GetCurrentContext()) return {};
  using GetString = const unsigned char* (*)(unsigned int);
  auto get = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
  if (!get) return {};
  auto str = [&](unsigned int name) {
    const unsigned char* v = get(name);
    return v ? std::string(reinterpret_cast<const char*>(v)) : std::string("?");
  };
  constexpr unsigned int kVendor = 0x1F00, kRenderer = 0x1F01,
                         kVersion = 0x1F02, kGlsl = 0x8B8C;
  return str(kVendor) + ", " + str(kRenderer) + ", " + str(kVersion) +
         ", GLSL " + str(kGlsl);
}

// Controllers with the mapping SDL actually applies and where its bindings
// were found: SDL versions differ in GUIDs (2.26+ add a CRC) and in which
// sources they read, so the mapping is printed rather than guessed.
std::vector<std::string> controller_lines() {
  std::vector<std::string> out;
  const char* env = SDL_getenv("SDL_GAMECONTROLLERCONFIG");
  std::string file_db;
  if (const char* file = SDL_getenv("SDL_GAMECONTROLLERCONFIG_FILE")) {
    std::ifstream in(file);
    file_db.assign(std::istreambuf_iterator<char>(in), {});
  }
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    char guid[33] = {};
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof(guid));
    const char* name = SDL_JoystickNameForIndex(i);
    const std::string head = "controller " + std::to_string(i);
    out.push_back(head + ": " + (name ? name : "(unnamed)") + ", GUID " + guid);
    char* m = SDL_IsGameController(i) ? SDL_GameControllerMappingForDeviceIndex(i)
                                      : nullptr;
    if (!m) {
      out.push_back(head + " mapping: none (plain joystick)");
      continue;
    }
    // "guid,name,bindings": compare the start of the bindings with the
    // sources (SDL may append fields such as crc: / platform:).
    std::string bindings = m;
    SDL_free(m);
    for (int skip = 0; skip < 2; ++skip)
      bindings.erase(0, std::min(bindings.size(), bindings.find(',') + 1));
    const std::string key = bindings.substr(0, 40);
    const char* source = "not in SDL_GAMECONTROLLERCONFIG(_FILE): SDL's database";
    if (env && std::strstr(env, key.c_str()))
      source = "SDL_GAMECONTROLLERCONFIG";
    else if (!file_db.empty() && file_db.find(key) != std::string::npos)
      source = "SDL_GAMECONTROLLERCONFIG_FILE";
    out.push_back(head + " mapping: " + source + ", " +
                  (bindings.size() > 120 ? bindings.substr(0, 120) + "…" : bindings));
  }
  if (out.empty()) out.push_back("controllers: none");
  return out;
}

std::string dir_line(const DirInfo& d) {
  if (d.path.empty()) return "(none)";
  return d.source.empty() ? d.path : d.path + " (" + d.source + ")";
}

}  // namespace

void print_startup_report(App& app) {
  const std::vector<std::string> libs = mapped_libraries();
  std::vector<std::string> out;
  out.push_back(std::string("xanatoo: ") + xanatoo::version_string() + ", " +
                build_type() + ", " + arch() + ", " + compiler());
  out.push_back("features: " + features());
  out.push_back("system: " + system_line());
  for (const std::string& e : env_lines()) out.push_back("env: " + e);

  SDL_version sv;
  SDL_GetVersion(&sv);
  out.push_back(
      "sdl: " +
      with_path(versions(version3(sv.major, sv.minor, sv.patch),
                         version3(SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                                  SDL_PATCHLEVEL)) +
                    " " + SDL_GetRevision(),
                mapped(libs, "libSDL2")));
  out.push_back("freetype: " +
                with_path(versions(freetype_runtime(),
                                   version3(FREETYPE_MAJOR, FREETYPE_MINOR,
                                            FREETYPE_PATCH)),
                          mapped(libs, "libfreetype")));
#if XANATOO_FONTCONFIG
  const int fc = FcGetVersion();
  out.push_back("fontconfig: " +
                with_path(versions(version3(fc / 10000, fc / 100 % 100, fc % 100),
                                   version3(FC_MAJOR, FC_MINOR, FC_REVISION)),
                          mapped(libs, "libfontconfig")));
#endif
  out.push_back("sqlite: " + with_path(versions(sqlite3_libversion(), SQLITE_VERSION),
                                       mapped(libs, "libsqlite3")));
#if XANATOO_HTTPS
  out.push_back("tls: " + with_path(xanatoo::TlsStream::version(),
                                    mapped(libs, "libssl")));
#endif
#if XANATOO_THUMTOO
  out.push_back("thumtoo: " + std::string(thumtoo::version_string()) + ", libvips " +
                with_path(xanatoo::image_backend_vips_version(),
                          mapped(libs, "libvips")));
#endif
  out.push_back("imgui: " IMGUI_VERSION);

  out.push_back("video: " + video_line(app));
  out.push_back("renderer: " + renderer_line(app));
  if (const std::string gl = opengl_line(); !gl.empty()) {
    out.push_back("opengl: " + gl);
    std::vector<std::string> drivers;
    for (const char* n : {"libGL", "libEGL", "libGLES", "libmali", "libMali"})
      if (const std::string p = mapped(libs, n); !p.empty()) drivers.push_back(p);
    for (const std::string& p : libs)
      if (p.find("_dri.so") != std::string::npos) drivers.push_back(p);
    if (!drivers.empty()) out.push_back("opengl libraries: " + join(drivers, ", "));
  }
  for (const std::string& c : controller_lines()) out.push_back(c);
  out.push_back("touch devices: " + std::to_string(SDL_GetNumTouchDevices()));

  out.push_back("dir config: " + dir_line(dir_info(DirKind::Config)));
  out.push_back("dir state: " + dir_line(dir_info(DirKind::State)));
  out.push_back("dir user data: " + dir_line(dir_info(DirKind::UserData)));
  out.push_back("dir cache: " + dir_line(dir_info(DirKind::Cache)));
  out.push_back("dir data: " + dir_line(data_dir_info()));
  if (no_save()) out.push_back("no-save: no config or state is written");

  for (const std::string& l : out) std::cerr << l << "\n";
  std::cerr.flush();
}

}  // namespace xanatoo::app
