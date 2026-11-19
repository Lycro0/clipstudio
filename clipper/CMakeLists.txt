cmake_minimum_required(VERSION 3.28...3.30)

# ClipStudio: a recording + instant-replay ("clip") frontend built on libobs.
# All streaming, scene editing, studio mode, docks, browser panels etc. are removed.

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil avformat swresample)

if(NOT TARGET OBS::libobs)
  add_subdirectory("${CMAKE_SOURCE_DIR}/libobs" libobs)
endif()

add_executable(clipstudio WIN32)

target_sources(
  clipstudio
  PRIVATE
    main.cpp
    AppSettings.cpp
    AppSettings.hpp
    AudioRemix.cpp
    AudioRemix.hpp
    ClipEditor.cpp
    ClipEditor.hpp
    ClipperEngine.cpp
    ClipperEngine.hpp
    HotkeyUtil.cpp
    HotkeyUtil.hpp
    MainWindow.cpp
    MainWindow.hpp
)

target_link_libraries(
  clipstudio
  PRIVATE OBS::libobs Qt6::Core Qt6::Widgets FFmpeg::avcodec FFmpeg::avutil FFmpeg::avformat FFmpeg::swresample
)

set_target_properties(clipstudio PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)

foreach(graphics_library IN ITEMS opengl metal d3d11)
  string(TOUPPER ${graphics_library} graphics_library_U)
  if(TARGET OBS::libobs-${graphics_library})
    target_compile_definitions(
      clipstudio
      PRIVATE
        DL_${graphics_library_U}="$<$<IF:$<PLATFORM_ID:Windows>,TARGET_FILE_NAME,TARGET_SONAME_FILE_NAME>:OBS::libobs-${graphics_library}>"
    )
  else()
    target_compile_definitions(clipstudio PRIVATE DL_${graphics_library_U}="")
  endif()
endforeach()

set_target_properties_obs(clipstudio PROPERTIES FOLDER frontend OUTPUT_NAME clipstudio)
