cmake_minimum_required(VERSION 3.20)

project(zelda3_3ds VERSION 3.0 LANGUAGES C)

set(ZELDA3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(ZELDA3_NATIVE "${ZELDA3_ROOT}/app/jni/src")
set(SDL2_ROOT "" CACHE PATH "Local SDL2 installation built for Nintendo 3DS")

if(NOT SDL2_ROOT)
  message(FATAL_ERROR "Set SDL2_ROOT to the local Nintendo 3DS SDL2 prefix")
endif()

find_package(SDL2 CONFIG REQUIRED
  PATHS "${SDL2_ROOT}/lib/cmake/SDL2"
  NO_DEFAULT_PATH
)

file(GLOB ZELDA3_ENGINE_SOURCES CONFIGURE_DEPENDS
  "${ZELDA3_NATIVE}/src/*.c"
  "${ZELDA3_NATIVE}/snes/*.c"
)

list(REMOVE_ITEM ZELDA3_ENGINE_SOURCES
  "${ZELDA3_NATIVE}/src/glsl_shader.c"
  "${ZELDA3_NATIVE}/src/opengl.c"
)

set(ZELDA3_3DS_SOURCES
  ${ZELDA3_ENGINE_SOURCES}
  "${ZELDA3_NATIVE}/src/platform/linux/second_screen_sdl.c"
  "${ZELDA3_NATIVE}/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/platform_3ds.c"
)

add_executable(zelda3-3ds ${ZELDA3_3DS_SOURCES})
set_source_files_properties(
  "${ZELDA3_NATIVE}/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c"
  PROPERTIES COMPILE_OPTIONS "-fno-fast-math"
)
set_target_properties(zelda3-3ds PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS YES
)

target_include_directories(zelda3-3ds PRIVATE
  "${ZELDA3_NATIVE}"
  "${ZELDA3_NATIVE}/src"
  "${ZELDA3_NATIVE}/third_party/opus-1.3.1-stripped"
  "${CMAKE_CURRENT_LIST_DIR}/source"
)

target_compile_definitions(zelda3-3ds PRIVATE
  SYSTEM_VOLUME_MIXER_AVAILABLE=0
  ZELDA3_3DS_VERSION=\"3.0-E5\"
  NDEBUG=1
)

target_compile_options(zelda3-3ds PRIVATE
  -O3
  -flto
  -ffast-math
  -ffunction-sections
  -fdata-sections
  -fno-strict-aliasing
  -fomit-frame-pointer
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-missing-field-initializers
)
target_link_options(zelda3-3ds PRIVATE -O3 -flto -Wl,--gc-sections)

target_link_libraries(zelda3-3ds PRIVATE
  SDL2::SDL2main
  SDL2::SDL2-static
  citro2d
  citro3d
  ctru
  m
)

set(ZELDA3_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${ZELDA3_ROMFS}")
configure_file(
  "${ZELDA3_ROOT}/app/src/main/assets/zelda3_assets.bps"
  "${ZELDA3_ROMFS}/zelda3_assets.bps"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/romfs/zelda3.ini"
  "${ZELDA3_ROMFS}/zelda3.ini"
  COPYONLY
)

set(ZELDA3_SMDH "${CMAKE_CURRENT_BINARY_DIR}/zelda3-3ds.smdh")
ctr_generate_smdh(
  OUTPUT "${ZELDA3_SMDH}"
  NAME "Zelda 3DS EXP 5"
  DESCRIPTION "A Link to the Past 3DS experimental 5"
  AUTHOR "EstebanPdN"
  ICON "${CMAKE_CURRENT_LIST_DIR}/assets/icon.png"
)

ctr_create_3dsx(zelda3-3ds-3dsx
  TARGET zelda3-3ds
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/zelda3-3ds-v3.0-E5.3dsx"
  SMDH "${ZELDA3_SMDH}"
  ROMFS "${ZELDA3_ROMFS}"
)
