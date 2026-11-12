cmake_minimum_required(VERSION 3.20)

project(zelda3_3ds VERSION 4.2 LANGUAGES C)

set(ZELDA3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(ZELDA3_NATIVE "${ZELDA3_ROOT}/engine")
option(ZELDA3_DEVELOPER_TESTS "Enable save-isolated developer scenarios and B+L cycling" OFF)
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
  "${ZELDA3_NATIVE}/src/rando/*.c"
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
  "${ZELDA3_NATIVE}/third_party/sha256/sha256.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/platform_3ds.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/apt_responsiveness.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/updater.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/update_manifest.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/ppu_gpu.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/ppu_gpu_model.c"
)

ctr_add_shader_library(alttp_pica_shader source/ppu_gpu.v.pica)
dkp_add_embedded_binary_library(alttp_pica_shader_bin alttp_pica_shader)

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

# The version the game shows and its updater compares: the project version,
# or an experimental label such as 4.0-E1.
set(ZELDA3_VERSION_LABEL "" CACHE STRING "Version shown by the game (empty: the project version)")
set(ZELDA3_SHOWN_VERSION "${ZELDA3_VERSION_LABEL}")
if(NOT ZELDA3_SHOWN_VERSION)
  set(ZELDA3_SHOWN_VERSION "${PROJECT_VERSION}")
endif()

target_compile_definitions(zelda3-3ds PRIVATE
  SYSTEM_VOLUME_MIXER_AVAILABLE=0
  ZELDA3_3DS_VERSION=\"${ZELDA3_SHOWN_VERSION}\"
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
target_link_options(zelda3-3ds PRIVATE -O3 -flto -Wl,--gc-sections -Wl,--wrap=GSPGPU_SetLcdForceBlack -Wl,--wrap=aptInit -Wl,--wrap=threadCreate)

set(UPDATE_DEPS_ROOT "$ENV{DEVKITPRO}/portlibs/3ds" CACHE PATH "3DS curl, mbedTLS and Jansson prefix")
target_include_directories(zelda3-3ds PRIVATE "${UPDATE_DEPS_ROOT}/include")
target_link_directories(zelda3-3ds PRIVATE "${UPDATE_DEPS_ROOT}/lib")

# The curl described in platform/3ds/update-dependencies is built without
# compression, but devkitPro's own 3ds-curl package enables zlib (and declares
# 3ds-zlib as a dependency), so its libcurl.a pulls in inflate/inflateEnd.
# Link zlib when the prefix has it, and leave the link line alone when it does
# not, so both prefixes build.
# Resolve the requested prefix to absolute archives. The SDK injects its own
# -L before target paths, so plain -lcurl can otherwise select a different
# curl (and different transitive dependencies) than the headers above.
set(ZELDA3_UPDATE_LIBRARIES)
foreach(dependency curl jansson mbedtls mbedx509 mbedcrypto)
  set(archive "${UPDATE_DEPS_ROOT}/lib/lib${dependency}.a")
  if(NOT EXISTS "${archive}")
    message(FATAL_ERROR "Missing 3DS update dependency: ${archive}")
  endif()
  list(APPEND ZELDA3_UPDATE_LIBRARIES "${archive}")
endforeach()
find_library(ZELDA3_3DS_ZLIB NAMES z
             HINTS "${UPDATE_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
if(ZELDA3_3DS_ZLIB)
  list(APPEND ZELDA3_UPDATE_LIBRARIES z)
endif()

target_link_libraries(zelda3-3ds PRIVATE
  SDL2::SDL2main
  SDL2::SDL2-static
  alttp_pica_shader_bin
  citro2d
  citro3d
  ${ZELDA3_UPDATE_LIBRARIES}
  ctru
  m
)

set(ZELDA3_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${ZELDA3_ROMFS}")
configure_file(
  "${ZELDA3_ROOT}/platform/3ds/romfs/zelda3_assets.bps"
  "${ZELDA3_ROMFS}/zelda3_assets.bps"
  COPYONLY
)
configure_file(
  "${ZELDA3_ROOT}/platform/3ds/romfs/zelda3_assets_fr-c.bps"
  "${ZELDA3_ROMFS}/zelda3_assets_fr-c.bps"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/romfs/zelda3.ini"
  "${ZELDA3_ROMFS}/zelda3.ini"
  COPYONLY
)

configure_file("${CMAKE_CURRENT_LIST_DIR}/romfs/update-ca.pem" "${ZELDA3_ROMFS}/update-ca.pem" COPYONLY)

# Language patches (romfs:/languages/<code>.bps), USA ROM -> translation; see
# romfs/languages/README.md. A language whose patch is missing is offered but
# reports itself unavailable when chosen.
file(REMOVE_RECURSE "${ZELDA3_ROMFS}/languages")
file(GLOB ZELDA3_LANGUAGE_PATCHES "${CMAKE_CURRENT_LIST_DIR}/romfs/languages/*.bps")
file(COPY ${ZELDA3_LANGUAGE_PATCHES} DESTINATION "${ZELDA3_ROMFS}/languages")
list(LENGTH ZELDA3_LANGUAGE_PATCHES ZELDA3_LANGUAGE_PATCH_COUNT)
message(STATUS "Language patches in the RomFS: ${ZELDA3_LANGUAGE_PATCH_COUNT}")

# Mods built into the RomFS (romfs:/mods/<mod>/), listed with those of the SD
# card: ZELDA3_BUNDLED_MODS is a folder of mod folders, each with its mod.ini.
# They are copied when CMake configures, which build.sh does on every run;
# an empty ZELDA3_BUNDLED_MODS leaves the RomFS without mods.
set(ZELDA3_BUNDLED_MODS "" CACHE PATH "Folder of mods to build into the RomFS")
file(REMOVE_RECURSE "${ZELDA3_ROMFS}/mods")
if(ZELDA3_BUNDLED_MODS)
  file(GLOB ZELDA3_BUNDLED_MOD_INIS "${ZELDA3_BUNDLED_MODS}/*/mod.ini")
  if(NOT ZELDA3_BUNDLED_MOD_INIS)
    message(FATAL_ERROR "No <mod>/mod.ini in ZELDA3_BUNDLED_MODS: ${ZELDA3_BUNDLED_MODS}")
  endif()
  foreach(ini IN LISTS ZELDA3_BUNDLED_MOD_INIS)
    get_filename_component(mod_dir "${ini}" DIRECTORY)
    file(COPY "${mod_dir}" DESTINATION "${ZELDA3_ROMFS}/mods")
  endforeach()
  list(LENGTH ZELDA3_BUNDLED_MOD_INIS ZELDA3_BUNDLED_MOD_COUNT)
  message(STATUS "Mods built into the RomFS: ${ZELDA3_BUNDLED_MOD_COUNT}")
endif()
# Developer scenarios are authored code and initialized from the user's ROM.
# Remove obsolete locally generated snapshots from an earlier build cache.
file(REMOVE_RECURSE "${ZELDA3_ROMFS}/scenarios")
set(ZELDA3_SMDH "${CMAKE_CURRENT_BINARY_DIR}/zelda3-3ds.smdh")
ctr_generate_smdh(
  OUTPUT "${ZELDA3_SMDH}"
  NAME "The Legend of Zelda"
  DESCRIPTION "A Link to the Past 3DS port"
  AUTHOR "EstebanPdN"
  ICON "${CMAKE_CURRENT_LIST_DIR}/assets/icon.png"
)

ctr_create_3dsx(zelda3-3ds-3dsx
  TARGET zelda3-3ds
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/zelda3-3ds.3dsx"
  SMDH "${ZELDA3_SMDH}"
  ROMFS "${ZELDA3_ROMFS}"
)

# Stable builds explicitly override host-fixture defaults.
target_compile_definitions(zelda3-3ds PRIVATE ZELDA3_ENABLE_DEVELOPER_TESTS=$<BOOL:${ZELDA3_DEVELOPER_TESTS}>)
