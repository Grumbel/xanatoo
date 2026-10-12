cmake_minimum_required(VERSION 3.16)
project(xanatoo VERSION 0.1.0 LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Catch common mistakes; ImGui is vendored and not under our control.
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# R36S cross builds pass SDL2_INCLUDE_DIRS / SDL2_LIBRARIES explicitly.
if(NOT SDL2_LIBRARIES)
  find_package(SDL2 REQUIRED)
endif()
if(NOT FREETYPE_LIBRARY AND NOT TARGET Freetype::Freetype)
  find_package(Freetype REQUIRED)
elseif(FREETYPE_LIBRARY AND NOT TARGET Freetype::Freetype)
  add_library(Freetype::Freetype UNKNOWN IMPORTED)
  set_target_properties(Freetype::Freetype PROPERTIES
    IMPORTED_LOCATION "${FREETYPE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FREETYPE_INCLUDE_DIRS}")
endif()
# Cross builds (R36S) may pass FONTCONFIG_* explicitly; otherwise pkg-config.
if(NOT FONTCONFIG_LIBRARIES)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
endif()

# Gemini protocol (TLS client). Parser for .gmi is always built.
option(XANATOO_GEMINI "Enable gemini:// network client (OpenSSL)" ON)
# Nostr read-only client (WSS + OpenSSL). Maps notes to Thread/Message nodes.
option(XANATOO_NOSTR "Enable Nostr note/thread fetch (OpenSSL WebSocket)" ON)
option(XANATOO_REDDIT "Enable Reddit .json fetch (OpenSSL HTTPS)" ON)
option(XANATOO_LEMMY "Enable Lemmy API v3 fetch (OpenSSL HTTPS)" ON)

add_executable(xanatoo
  src/main.cpp
  src/font.cpp
  src/document.cpp
  src/layout.cpp
  src/render.cpp
  src/parse.cpp
  src/org.cpp
  src/editor.cpp
  src/images.cpp
  src/ui.cpp
  src/util.cpp
  src/minibuffer.cpp
  src/sidestore.cpp
  src/cli.cpp
  src/gemini.cpp
  src/nostr.cpp
  src/reddit.cpp
  src/lemmy.cpp
  # Vendored Dear ImGui (MIT) — menu bar / toolbar shell
  external/imgui/imgui.cpp
  external/imgui/imgui_draw.cpp
  external/imgui/imgui_tables.cpp
  external/imgui/imgui_widgets.cpp
  external/imgui/backends/imgui_impl_sdl2.cpp
  external/imgui/backends/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(xanatoo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/external
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
  ${SDL2_INCLUDE_DIRS}
  ${FREETYPE_INCLUDE_DIRS}
  ${FONTCONFIG_INCLUDE_DIRS}
)

find_package(SQLite3 REQUIRED)

target_link_libraries(xanatoo PRIVATE
  ${SDL2_LIBRARIES}
  SQLite::SQLite3
  Freetype::Freetype
  ${FONTCONFIG_LIBRARIES}
)

set(XANATOO_NEED_OPENSSL OFF)
if(XANATOO_GEMINI OR XANATOO_NOSTR OR XANATOO_REDDIT OR XANATOO_LEMMY)
  set(XANATOO_NEED_OPENSSL ON)
endif()

if(XANATOO_NEED_OPENSSL)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(xanatoo PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_sources(xanatoo PRIVATE src/https.cpp)
endif()

if(XANATOO_GEMINI)
  target_compile_definitions(xanatoo PRIVATE XANATOO_GEMINI=1)
  message(STATUS "Gemini protocol client: ON (OpenSSL)")
else()
  target_compile_definitions(xanatoo PRIVATE XANATOO_GEMINI=0)
  message(STATUS "Gemini protocol client: OFF")
endif()

if(XANATOO_NOSTR)
  target_compile_definitions(xanatoo PRIVATE XANATOO_NOSTR=1)
  message(STATUS "Nostr client: ON (OpenSSL WSS)")
else()
  target_compile_definitions(xanatoo PRIVATE XANATOO_NOSTR=0)
  message(STATUS "Nostr client: OFF")
endif()

if(XANATOO_REDDIT)
  target_compile_definitions(xanatoo PRIVATE XANATOO_REDDIT=1)
  message(STATUS "Reddit .json client: ON (OpenSSL HTTPS)")
else()
  target_compile_definitions(xanatoo PRIVATE XANATOO_REDDIT=0)
  message(STATUS "Reddit .json client: OFF")
endif()

if(XANATOO_LEMMY)
  target_compile_definitions(xanatoo PRIVATE XANATOO_LEMMY=1)
  message(STATUS "Lemmy API client: ON (OpenSSL HTTPS)")
else()
  target_compile_definitions(xanatoo PRIVATE XANATOO_LEMMY=0)
  message(STATUS "Lemmy API client: OFF")
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(xanatoo PRIVATE m)
endif()

install(TARGETS xanatoo DESTINATION bin)

# Toolbar icons: SVG sources → PNG at build time (ImageMagick or pure-Python fallback)
set(XANATOO_SVG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/icons/svg)
set(XANATOO_ICON_DIR ${CMAKE_CURRENT_BINARY_DIR}/icons)
file(GLOB XANATOO_SVGS "${XANATOO_SVG_DIR}/*.svg")
add_custom_command(
  OUTPUT ${XANATOO_ICON_DIR}/.stamp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${XANATOO_ICON_DIR}
  COMMAND ${CMAKE_COMMAND} -E env python3
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/rasterize_icons.py
          ${XANATOO_SVG_DIR} ${XANATOO_ICON_DIR} 20
  COMMAND ${CMAKE_COMMAND} -E touch ${XANATOO_ICON_DIR}/.stamp
  DEPENDS ${XANATOO_SVGS} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/rasterize_icons.py
  COMMENT "Rasterizing toolbar icons (SVG → PNG)"
  VERBATIM
)
add_custom_target(xanatoo_icons ALL DEPENDS ${XANATOO_ICON_DIR}/.stamp)
add_dependencies(xanatoo xanatoo_icons)

# Build-tree path for dev runs. Installed layout is found at runtime via
# XANATOO_ICON_DIR env or <exe>/../share/xanatoo/icons (see Ui::load_icons).
target_compile_definitions(xanatoo PRIVATE
  "XANATOO_ICON_DIR=\"${XANATOO_ICON_DIR}\"")

install(DIRECTORY ${XANATOO_ICON_DIR}/
        DESTINATION share/xanatoo/icons
        FILES_MATCHING PATTERN "*.png"
        PATTERN ".stamp" EXCLUDE)
install(DIRECTORY icons/svg/
        DESTINATION share/xanatoo/icons/svg
        FILES_MATCHING PATTERN "*.svg")
install(DIRECTORY examples/
        DESTINATION share/xanatoo/examples
        FILES_MATCHING
          PATTERN "*.md"
          PATTERN "*.png"
          PATTERN "*.jpg"
          PATTERN "*.jpeg"
          PATTERN "*.gif"
          PATTERN "*.sexp"
          PATTERN "*.gmi"
          PATTERN "*.gemini"
          PATTERN "*.org")


# ---------------------------------------------------------------------------
# Unit tests (no network; URL/string/JSON helpers only)
# ---------------------------------------------------------------------------
add_executable(xanatoo_tests
  tests/unit_tests.cpp
  src/util.cpp
  src/minibuffer.cpp
  src/document.cpp
  src/parse.cpp
  src/org.cpp
  src/reddit.cpp
  src/lemmy.cpp
  src/nostr.cpp
  src/gemini.cpp
)
target_include_directories(xanatoo_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# Detection/parse APIs compile without OpenSSL clients.
target_compile_definitions(xanatoo_tests PRIVATE
  XANATOO_GEMINI=0
  XANATOO_NOSTR=0
  XANATOO_REDDIT=0
  XANATOO_LEMMY=0
)
enable_testing()
add_test(NAME unit_tests COMMAND xanatoo_tests)
