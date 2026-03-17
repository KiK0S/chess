add_library(lunasvg STATIC
  src/lunasvg.cpp
)

target_include_directories(lunasvg
  PUBLIC
    include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/third-party/nanosvg
)

target_compile_features(lunasvg PUBLIC cxx_std_20)
