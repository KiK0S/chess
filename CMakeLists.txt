cmake_minimum_required(VERSION 3.20)

project(chess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(ENGINE_PLATFORM "web")
else()
  set(ENGINE_PLATFORM "native")
endif()

set(FETCHCONTENT_SOURCE_DIR_LUNASVG "${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/lunasvg")

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/stb")

add_subdirectory(3rd-party/engine/libs/core)
add_subdirectory(3rd-party/engine/libs/ui)
add_subdirectory(3rd-party/engine/libs/render)

if(ENGINE_PLATFORM STREQUAL "web")
  target_compile_options(engine_webgl PRIVATE "-sUSE_LIBPNG=1")
  target_link_options(engine_webgl PRIVATE "-sUSE_LIBPNG=1")
else()
  find_package(PNG REQUIRED)
  target_link_libraries(engine_webgl PRIVATE PNG::PNG)
endif()

add_subdirectory(3rd-party/engine/libs/platform)

if(ENGINE_PLATFORM STREQUAL "web")
  target_compile_options(engine_platform PUBLIC "-sUSE_SDL=2")

  # The engine target currently mixes native and web platform backends.
  # Web builds should compile only the Emscripten backend.
  get_target_property(engine_platform_sources engine_platform SOURCES)
  list(FILTER engine_platform_sources EXCLUDE REGEX "platform_sdl\\.cpp$")
  set_property(TARGET engine_platform PROPERTY SOURCES "${engine_platform_sources}")
endif()

add_executable(chess
  src/main.cpp
  src/chess/ai.cpp
  src/chess/game.cpp
)

target_include_directories(chess PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(chess
  PRIVATE
    engine_core
    engine_ui
    engine_render
    engine_canvas2d
    engine_platform
)
target_compile_features(chess PRIVATE cxx_std_20)

add_custom_command(TARGET chess POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:chess>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/chess
    $<TARGET_FILE_DIR:chess>/assets/chess)

if(ENGINE_PLATFORM STREQUAL "web")
  set_target_properties(chess PROPERTIES SUFFIX ".html")
  target_link_options(chess PRIVATE
    "-sUSE_LIBPNG=1"
    "-sWASM=1"
    "--use-preload-plugins"
    "-sMIN_WEBGL_VERSION=2"
    "-sMAX_WEBGL_VERSION=2"
    "-sUSE_WEBGL2=1"
    "-sFULL_ES3=1"
    "-sUSE_SDL=2"
    "--preload-file"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/chess@/assets/chess")
endif()
