cmake_minimum_required(VERSION 3.16)
project(glyphtoo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)
find_package(PkgConfig REQUIRED)

add_executable(glyphtoo
  src/main.cpp
  src/font.cpp
  src/document.cpp
  src/layout.cpp
  src/render.cpp
  src/parse.cpp
  src/editor.cpp
  src/images.cpp
)

target_include_directories(glyphtoo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/external
  ${SDL2_INCLUDE_DIRS}
  ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(glyphtoo PRIVATE
  ${SDL2_LIBRARIES}
  Freetype::Freetype
)

if(UNIX AND NOT APPLE)
  target_link_libraries(glyphtoo PRIVATE m)
endif()

install(TARGETS glyphtoo DESTINATION bin)
