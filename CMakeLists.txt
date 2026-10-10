cmake_minimum_required(VERSION 3.16)
project(xanatoo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Catch common mistakes; ImGui is vendored and not under our control.
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

add_executable(xanatoo
  src/main.cpp
  src/font.cpp
  src/document.cpp
  src/layout.cpp
  src/render.cpp
  src/parse.cpp
  src/editor.cpp
  src/images.cpp
  src/ui.cpp
  src/util.cpp
  src/cli.cpp
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

target_link_libraries(xanatoo PRIVATE
  ${SDL2_LIBRARIES}
  Freetype::Freetype
  ${FONTCONFIG_LIBRARIES}
)

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
          PATTERN "*.sexp")
