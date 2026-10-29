cmake_minimum_required(VERSION 3.16)
project(BO1Zombies)

set(BIN_NAME "BO1Zombies")

set(CMAKE_CXX_STANDARD 20)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(TL_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/tl")
set(DW_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/DemonWare")

include(cmake_files.cmake)

# This fork is an Android port: the browser (Emscripten) and Windows (MSVC) builds were removed; the upstream
# repositories keep them (README.md, Credits). cmake_files.cmake lists the engine's sources (the target above);
# cmake/native.cmake builds them for Android (and Linux, for testing), as tools/native/build-android.sh configures it.

option(BO1_NATIVE "Native POSIX build (Android; Linux for testing)" OFF)
if (NOT BO1_NATIVE)
    message(FATAL_ERROR "Configure with -DBO1_NATIVE=ON (tools/native/build-android.sh). The Windows build is not part of this fork.")
endif()
include(cmake/native.cmake)
