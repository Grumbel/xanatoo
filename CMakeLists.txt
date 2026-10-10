cmake_minimum_required(VERSION 3.16)
project(glyphtoo VERSION 0.1.0 LANGUAGES CXX)

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

add_executable(glyphtoo
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

target_include_directories(glyphtoo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/external
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
  ${SDL2_INCLUDE_DIRS}
  ${FREETYPE_INCLUDE_DIRS}
  ${FONTCONFIG_INCLUDE_DIRS}
)

target_link_libraries(glyphtoo PRIVATE
  ${SDL2_LIBRARIES}
  Freetype::Freetype
  ${FONTCONFIG_LIBRARIES}
)

if(UNIX AND NOT APPLE)
  target_link_libraries(glyphtoo PRIVATE m)
endif()

install(TARGETS glyphtoo DESTINATION bin)

# Toolbar icons: SVG sources → PNG at build time (ImageMagick or pure-Python fallback)
set(GLYPHTOO_SVG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/icons/svg)
set(GLYPHTOO_ICON_DIR ${CMAKE_CURRENT_BINARY_DIR}/icons)
file(GLOB GLYPHTOO_SVGS "${GLYPHTOO_SVG_DIR}/*.svg")
add_custom_command(
  OUTPUT ${GLYPHTOO_ICON_DIR}/.stamp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GLYPHTOO_ICON_DIR}
  COMMAND ${CMAKE_COMMAND} -E env python3
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/rasterize_icons.py
          ${GLYPHTOO_SVG_DIR} ${GLYPHTOO_ICON_DIR} 20
  COMMAND ${CMAKE_COMMAND} -E touch ${GLYPHTOO_ICON_DIR}/.stamp
  DEPENDS ${GLYPHTOO_SVGS} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/rasterize_icons.py
  COMMENT "Rasterizing toolbar icons (SVG → PNG)"
  VERBATIM
)
add_custom_target(glyphtoo_icons ALL DEPENDS ${GLYPHTOO_ICON_DIR}/.stamp)
add_dependencies(glyphtoo glyphtoo_icons)

# Runtime can find icons next to the binary or via compile-time path.
target_compile_definitions(glyphtoo PRIVATE
  "GLYPHTOO_ICON_DIR=\"${GLYPHTOO_ICON_DIR}\"")

install(DIRECTORY ${GLYPHTOO_ICON_DIR}/
        DESTINATION share/glyphtoo/icons
        FILES_MATCHING PATTERN "*.png"
        PATTERN ".stamp" EXCLUDE)
install(DIRECTORY icons/svg/
        DESTINATION share/glyphtoo/icons/svg
        FILES_MATCHING PATTERN "*.svg")
install(DIRECTORY examples/
        DESTINATION share/glyphtoo/examples
        FILES_MATCHING
          PATTERN "*.md"
          PATTERN "*.png"
          PATTERN "*.jpg"
          PATTERN "*.jpeg"
          PATTERN "*.gif"
          PATTERN "*.sexp")
