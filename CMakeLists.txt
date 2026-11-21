cmake_minimum_required(VERSION 3.21)

project(ClipStudio VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "ClipStudio is a Windows application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# FFmpeg (shared build, e.g. https://github.com/BtbN/FFmpeg-Builds) - point FFMPEG_DIR at it.
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/deps/ffmpeg" CACHE PATH "FFmpeg root (include/, lib/, bin/)")
set(FFMPEG_LIBS avcodec avformat avutil swresample swscale)
add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg INTERFACE "${FFMPEG_DIR}/include")
foreach(lib IN LISTS FFMPEG_LIBS)
  find_library(FFMPEG_${lib}_LIB NAMES ${lib} PATHS "${FFMPEG_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
  target_link_libraries(ffmpeg INTERFACE "${FFMPEG_${lib}_LIB}")
endforeach()

add_executable(ClipStudio WIN32
  src/main.cpp
  src/AppSettings.cpp src/AppSettings.hpp
  src/AudioCapture.cpp src/AudioCapture.hpp
  src/AudioRemix.cpp src/AudioRemix.hpp
  src/ClipEditor.cpp src/ClipEditor.hpp
  src/Engine.cpp src/Engine.hpp
  src/Hotkeys.cpp src/Hotkeys.hpp
  src/MainWindow.cpp src/MainWindow.hpp
  src/Muxer.cpp src/Muxer.hpp
  src/Theme.cpp src/Theme.hpp
  src/VideoCapture.cpp src/VideoCapture.hpp
  src/WinUtil.cpp src/WinUtil.hpp
  src/app.rc
)

target_compile_definitions(ClipStudio PRIVATE
  UNICODE _UNICODE NOMINMAX
  WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A00000C
  CLIPSTUDIO_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(ClipStudio PRIVATE /W3 /permissive- /Zc:__cplusplus /utf-8)
endif()

target_link_libraries(ClipStudio PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets ffmpeg
  d3d11 dxgi windowsapp mmdevapi ole32 winmm)
