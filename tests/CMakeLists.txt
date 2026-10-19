cmake_minimum_required(VERSION 3.16)
project(csgo_format_tests C CXX)
set(CMAKE_CXX_STANDARD 17)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)
find_package(ZLIB REQUIRED)

add_executable(format_tests
    format_tests.cpp
    ${SRC}/core/log.cpp
    ${SRC}/fs/vfs.cpp
    ${SRC}/format/lzma_valve.cpp
    ${SRC}/format/bsp.cpp
    ${SRC}/format/vtf.cpp
    ${SRC}/format/vmt.cpp
    ${SRC}/game/world.cpp
    ${SRC}/game/collision.cpp
    ${SRC}/game/player.cpp
    ${SRC}/third_party/lzma/LzmaDec.c
    third_party/LzmaEnc.c
    third_party/LzFind.c)
target_include_directories(format_tests PRIVATE ${SRC}/third_party/lzma third_party)
target_compile_definitions(format_tests PRIVATE _7ZIP_ST)
target_compile_options(format_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter>)
target_link_libraries(format_tests PRIVATE ZLIB::ZLIB pthread)
