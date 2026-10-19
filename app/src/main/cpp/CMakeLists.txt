cmake_minimum_required(VERSION 3.22.1)
project(csgo_mobile C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# adrenotools (memuat driver Turnip/Mesa tanpa root) + hook-nya (libmain_hook.so, dll.)
# Hanya arm64 (perangkat Adreno). Build x86_64 dipakai untuk uji asap di emulator CI.
if(ANDROID_ABI STREQUAL "arm64-v8a")
    add_subdirectory(third_party/adrenotools)
    set(HAS_ADRENOTOOLS ON)
endif()

# LZMA SDK (domain publik) — dekoder saja
add_library(lzma STATIC third_party/lzma/LzmaDec.c)
target_include_directories(lzma PUBLIC third_party/lzma)
target_compile_definitions(lzma PRIVATE _7ZIP_ST)

# volk — memuat fungsi Vulkan secara dinamis dari libvulkan pilihan (sistem / Turnip)
add_library(volk STATIC third_party/volk/volk.c)
target_include_directories(volk BEFORE PUBLIC third_party/vulkan/include third_party/volk)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR)

add_library(csgo SHARED
    jni_bridge.cpp
    core/log.cpp
    fs/vfs.cpp
    format/lzma_valve.cpp
    format/bsp.cpp
    format/vtf.cpp
    format/vmt.cpp
    game/world.cpp
    game/collision.cpp
    game/player.cpp
    game/game.cpp
    render/renderer.cpp)

target_include_directories(csgo PRIVATE . third_party/adrenotools/include)
target_compile_options(csgo PRIVATE -Wall -Wno-unused-parameter -Wno-missing-field-initializers -O2)
target_link_libraries(csgo PRIVATE volk lzma android log z dl)
if(HAS_ADRENOTOOLS)
    target_link_libraries(csgo PRIVATE adrenotools)
    target_compile_definitions(csgo PRIVATE CSGO_HAS_ADRENOTOOLS=1)
endif()
