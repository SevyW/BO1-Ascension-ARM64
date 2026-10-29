# cmake/native.cmake - the native (non-Windows, non-browser) build of BO1Zombies: Linux now, Android later.
#
# Included by CMakeLists.txt when configured with -DBO1_NATIVE=ON. It is the web build (cmake/web.cmake) without
# Emscripten: the same engine sources, the same Win32 emulation (src/web/compat, plain pthreads/POSIX) and the same
# D3D9 shim (src/web/d3d9, OpenGL ES 3), with src/native/ in place of the browser glue. BO1_WEB stays defined (every
# "not Windows" change in shared code is keyed on it), BO1_WEB_NODE too (no page, plain main loop, host file system);
# BO1_NATIVE marks what differs from the Emscripten build.
#
#   cmake -S . -B build/native-x86 -G Ninja -DBO1_NATIVE=ON -DBO1_NATIVE_ARCH=x86 \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#
# Output: build/<dir>/bin/bo1

set(WEB_DIR "${SRC_DIR}/web")
set(NATIVE_DIR "${SRC_DIR}/native")

set(BO1_NATIVE_ARCH "x86" CACHE STRING "x86 (32-bit, -m32: the engine's original pointer size) or host (compiler default)")
option(BO1_NATIVE_RENDERER "Compile src/gfx_d3d against the D3D9 shim" ON)
set(BO1_NATIVE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Where the bo1 executable is written")
# Android (arm64; x64 for the emulator): the engine becomes libbo1.so, position independent, loaded below 4 GB by the stub in
# src/native/android/bo1_boot.c (libmain.so for SDL's SDLActivity, and bo1_boot_test for trying it without an app).
# Configure with the compiler target aarch64-linux-android<api>, the NDK sysroot, and BO1_ANDROID_SDL2 = the
# directory that has SDL2's include/ and libSDL2.so (tools/native/build-android.sh does all of it).
option(BO1_NATIVE_ANDROID "Build the Android engine library and its loader stub" OFF)
set(BO1_ANDROID_SDL2 "" CACHE PATH "Android: directory with SDL2 include/ and lib/libSDL2.so")

# ----- Sources: the engine's list (cmake_files.cmake) minus what only Windows uses -----
set(BO1_NATIVE_EXCLUDE
    "/src/win32/"
    "/src/tracy/"
    "/src/binklib/"
    "/src/mjpeg/"
    "/src/vpx/"
    "/src/nvapi/"
    "/src/CubeMapGenLib/"
    "/src/groupvoice/"
    "/src/monkey/"
    "/src/sound/snd_driver_xaudio2[^/]*\\.cpp$"
)
if (NOT BO1_NATIVE_RENDERER)
    list(APPEND BO1_NATIVE_EXCLUDE "/src/gfx_d3d/")
endif()
set(BO1_NATIVE_SHARED_WIN32
    win_common.cpp
    win_configure.cpp
    win_content.cpp
    win_gamerprofile.cpp
    win_localize.cpp
    win_splash.cpp
    win_steam.cpp
    win_stream.cpp
    win_tasks.cpp
    win_workercmds.cpp
)

get_target_property(_bo1_all_sources ${BIN_NAME} SOURCES)
set(BO1_ENGINE_SOURCES ${_bo1_all_sources})
foreach (_re IN LISTS BO1_NATIVE_EXCLUDE)
    list(FILTER BO1_ENGINE_SOURCES EXCLUDE REGEX "${_re}")
endforeach()
foreach (_f IN LISTS BO1_NATIVE_SHARED_WIN32)
    list(APPEND BO1_ENGINE_SOURCES "${SRC_DIR}/win32/${_f}")
endforeach()
list(FILTER BO1_ENGINE_SOURCES INCLUDE REGEX "\\.(c|cpp)$")

# src/web/ platform files that are not browser glue, the Win32 emulation, and src/native/ (what replaces the glue)
file(GLOB BO1_NATIVE_PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${WEB_DIR}/*.cpp"
    "${WEB_DIR}/compat/*.cpp"
    "${NATIVE_DIR}/*.cpp"
)

# ----- Architecture -----
set(BO1_NATIVE_ARCH_FLAGS "")
if (BO1_NATIVE_ARCH STREQUAL "x86")
    # -malign-double: MSVC x86 aligns double and __int64 struct members to 8 (the i386 System V ABI: 4); the engine's
    # struct layouts (fastfile assets, static_assert(sizeof) checks) are MSVC's. libc structs that hold 64-bit
    # members at 4-byte offsets (struct stat) must not be touched from these TUs: src/native/native_posix.c is
    # compiled without the flag and is the only user.
    # -mlong-double-64: long double is double, as in MSVC (and x87's 12-byte long double cannot be 8-aligned).
    # -mavx: VEX encoding, so that only explicit aligned moves can fault on unaligned data and
    # src/native/native_x86_unaligned.cpp can fix them up (decompiled code dereferences unaligned vectors).
    set(BO1_NATIVE_ARCH_FLAGS -m32 -msse -msse2 -mavx -mfpmath=sse -malign-double -mlong-double-64)
    set(BO1_NATIVE_ARCH_FLAGS_PLAIN -m32 -msse -msse2 -mfpmath=sse)
    set(BO1_NATIVE_ARCH_LINK -m32)
elseif (BO1_NATIVE_ARCH STREQUAL "x64")
    # 64-bit x86: double and __int64 already align to 8. long double stays a double (MSVC); -mavx for the unaligned
    # move fix-up as on x86.
    # No auto-vectorization: LLVM's vectorizers build vectors of 32-bit pointers and the backend cannot convert them
    # ("Cannot select: v4i64 = addrspacecast[271 -> 0]").
    # memset / memcpy / memmove written as calls stay calls: turned into intrinsics, the optimizer folds the
    # widening of a 32-bit pointer argument into them and the backend then cannot lower the libcall ("cannot
    # lower memory intrinsic in address space 271").
    set(BO1_NATIVE_ARCH_FLAGS -mavx -mlong-double-64 -fno-vectorize -fno-slp-vectorize
        -fno-builtin-memset -fno-builtin-memcpy -fno-builtin-memmove
        # no switch -> lookup table conversion: a table of 32-bit pointers is a constant LLVM cannot emit
        # ("unsupported expression in static initializer: addrspacecast"). Jump tables are unaffected.
        -mllvm -switch-to-lookup=false)
    set(BO1_NATIVE_ARCH_FLAGS_PLAIN "")
    set(BO1_NATIVE_ARCH_LINK "")
    # 64-bit builds compile the 32-bit-pointer copy of the tree (tools/p32/p32tree.py). native_p32.h defines what
    # the rewriter inserts (BO1_P32, BO1_NAT, BO1_ADDR) and is force-included everywhere.
    set(BO1_NATIVE_P32 ON)
elseif (BO1_NATIVE_ARCH STREQUAL "arm64")
    # AArch64 (Linux now, Android next): the 32-bit-pointer build again, with the same LLVM workarounds as x64
    # (see there). SSE is sse2neon (src/web/compat/xmmintrin.h); NEON loads do not fault on unaligned data, so
    # there is no fix-up. Cross builds pass the target and sysroot on the cmake command line.
    # There is no -mlong-double-64 for AArch64: tools/p32 turns the engine's `long double` into `double`.
    # -fsigned-char: char is signed to this code (MSVC, x86); the AArch64 ABI's is unsigned.
    set(BO1_NATIVE_ARCH_FLAGS -fsigned-char -fno-vectorize -fno-slp-vectorize
        -fno-builtin-memset -fno-builtin-memcpy -fno-builtin-memmove
        -mllvm -switch-to-lookup=false)
    set(BO1_NATIVE_ARCH_FLAGS_PLAIN -fsigned-char)
    set(BO1_NATIVE_P32 ON)
else()
    set(BO1_NATIVE_ARCH_FLAGS_PLAIN "")
    set(BO1_NATIVE_ARCH_LINK "")
endif()

# The 32-bit-pointer builds need their image below 4 GB, so that functions, globals and string literals have
# addresses a 32-bit pointer holds.
if (BO1_NATIVE_P32)
    if (BO1_NATIVE_ANDROID)
        # a shared library: the stub puts it below 4 GB at load time. -Bsymbolic: its malloc is its own
        # (native_lowmem.c); libc++ static (named with the libraries below) so that operator new is that malloc too.
        list(APPEND BO1_NATIVE_ARCH_FLAGS -fPIC)
        list(APPEND BO1_NATIVE_ARCH_FLAGS_PLAIN -fPIC)
        set(BO1_NATIVE_ARCH_LINK -shared -nostdlib++ "-Wl,-Bsymbolic" "-Wl,-soname,libbo1.so"
            "-Wl,-z,max-page-size=16384" "-Wl,--no-undefined")
    else()
        # Linux: a fixed (non-PIE) link at the default low base
        list(APPEND BO1_NATIVE_ARCH_FLAGS -fno-pie)
        list(APPEND BO1_NATIVE_ARCH_FLAGS_PLAIN -fno-pie)
        set(BO1_NATIVE_ARCH_LINK -no-pie)
    endif()
endif()

set(BO1_NATIVE_COMMON_FLAGS
    ${BO1_NATIVE_ARCH_FLAGS}
    -pthread
    -stdlib=libc++                         # the C++ library of the Emscripten build and of the Android NDK
    -fms-extensions -fdeclspec
    -mms-bitfields
    -fno-strict-aliasing
    -fwrapv
    -fno-delete-null-pointer-checks
    -include "${WEB_DIR}/compat/bo1_web_prelude.h"
    -Wno-c++11-narrowing
    -Wno-register
    -Wno-reserved-user-defined-literal
    -Wno-dynamic-exception-spec
    -Wno-incompatible-pointer-types
    -Wno-incompatible-function-pointer-types
    -Wno-int-conversion
    -Wno-implicit-function-declaration
    -Wno-implicit-int
    -Wno-return-mismatch
    -Wno-address-of-temporary
    -Wno-invalid-noreturn
    -Wno-ignored-attributes
    -Wno-microsoft
    -Wno-pragma-pack
    -Wno-unknown-pragmas
    -Wno-parentheses
)
set(BO1_NATIVE_DEFINES
    BO1_WEB
    BO1_WEB_NODE
    BO1_NATIVE
    BO1_MP
    _CONSOLE
    _CRT_SECURE_NO_WARNINGS
    _GNU_SOURCE
    _FILE_OFFSET_BITS=64
    _TIME_BITS=64                          # 64-bit time_t on 32-bit glibc, like MSVC's and Emscripten's
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# ----- D3D9 shim (OpenGL ES 3) -----
if (BO1_NATIVE_RENDERER)
    include("${WEB_DIR}/d3d9/d3d9shim.cmake")
    target_include_directories(bo1_d3d9shim BEFORE PRIVATE "${WEB_DIR}/compat")
    target_compile_definitions(bo1_d3d9shim PRIVATE BO1_WEB BO1_NATIVE NOMINMAX _GNU_SOURCE _FILE_OFFSET_BITS=64 _TIME_BITS=64
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
    target_compile_options(bo1_d3d9shim PRIVATE ${BO1_NATIVE_ARCH_FLAGS} -pthread -stdlib=libc++ -fno-strict-aliasing -fms-extensions
        -fdeclspec -Wno-ignored-attributes -Wno-microsoft)
    target_link_libraries(${BIN_NAME} PRIVATE bo1_d3d9shim)
endif()

# ----- Vulkan renderer: DXVK's native D3D9 behind a bridge -----
# src/web/d3d9/d3d9_vkbridge.cpp (the engine's interfaces, in the shim library) calls src/native/vk/bo1vk_dxvk.cpp,
# which is compiled against DXVK's headers and loads libdxvk_d3d9.so at run time. BO1_NATIVE_DXVK = the DXVK source
# tree (only its include/native headers are used here); empty = OpenGL ES only.
set(BO1_NATIVE_DXVK "" CACHE PATH "DXVK source tree; empty = no Vulkan renderer")
if (BO1_NATIVE_RENDERER AND BO1_NATIVE_DXVK)
    add_library(bo1_vkbridge STATIC "${NATIVE_DIR}/vk/bo1vk_dxvk.cpp")
    target_include_directories(bo1_vkbridge PRIVATE "${BO1_NATIVE_DXVK}/include/native/directx"
        "${BO1_NATIVE_DXVK}/include/native/windows")
    target_compile_definitions(bo1_vkbridge PRIVATE BO1_DXVK)
    target_compile_options(bo1_vkbridge PRIVATE ${BO1_NATIVE_ARCH_FLAGS_PLAIN} -pthread -stdlib=libc++ -w)
    target_compile_definitions(bo1_d3d9shim PRIVATE BO1_DXVK)
    target_compile_definitions(${BIN_NAME} PRIVATE BO1_DXVK)   # the graphics menu's renderer row (ui/ui_main.cpp)
    target_link_libraries(${BIN_NAME} PRIVATE bo1_vkbridge)
endif()

# ----- xWMA sounds: FFmpeg's WMA 2 decoder in the mixer (src/native/native_audio.cpp) -----
# BO1_NATIVE_FFMPEG = a directory with include/ and lib/libavcodec.a, lib/libavutil.a (tools/native/build-ffmpeg.sh);
# empty = the game's xWMA sounds (most effects on PC) stay silent.
set(BO1_NATIVE_FFMPEG "" CACHE PATH "FFmpeg build with the wmav2 decoder; empty = no xWMA sounds")
if (BO1_NATIVE_FFMPEG)
    target_compile_definitions(${BIN_NAME} PRIVATE BO1_FFMPEG)
    target_include_directories(${BIN_NAME} PRIVATE "${BO1_NATIVE_FFMPEG}/include")
    target_link_libraries(${BIN_NAME} PRIVATE "${BO1_NATIVE_FFMPEG}/lib/libavcodec.a" "${BO1_NATIVE_FFMPEG}/lib/libavutil.a")
endif()

# native_posix.c: libc calls whose structs do not survive -malign-double (see above); plain ABI flags
set(BO1_NATIVE_PLAIN_SOURCES "${NATIVE_DIR}/native_posix.c" "${NATIVE_DIR}/native_lowmem.c"
    "${NATIVE_DIR}/native_stdio.c")

# native_longdouble.c: cosl & co. for long double == double (-mlong-double-64); compiled with the engine's ABI flags
set(BO1_NATIVE_ABI_C_SOURCES "${NATIVE_DIR}/native_longdouble.c")
set_source_files_properties(${BO1_NATIVE_ABI_C_SOURCES} PROPERTIES COMPILE_OPTIONS "${BO1_NATIVE_ARCH_FLAGS};-fno-builtin")

set_property(TARGET ${BIN_NAME} PROPERTY SOURCES ${BO1_ENGINE_SOURCES} ${BO1_NATIVE_PLATFORM_SOURCES}
    ${BO1_NATIVE_PLAIN_SOURCES} ${BO1_NATIVE_ABI_C_SOURCES})
set_source_files_properties(${BO1_ENGINE_SOURCES} PROPERTIES COMPILE_OPTIONS "${BO1_NATIVE_COMMON_FLAGS};-w")
set_source_files_properties(${BO1_NATIVE_PLATFORM_SOURCES} PROPERTIES COMPILE_OPTIONS
    "${BO1_NATIVE_COMMON_FLAGS};-Wno-inconsistent-missing-override;-Wno-missing-declarations;-Wno-invalid-offsetof;-Wno-deprecated-declarations;-Wno-dangling-else;-Wno-switch;-Wno-nonportable-include-path;-Wno-deprecated-volatile;-Wno-array-bounds;-Wno-nontrivial-memcall;-Wno-void-pointer-to-int-cast")
set_source_files_properties(${BO1_NATIVE_PLAIN_SOURCES} PROPERTIES COMPILE_OPTIONS
    "${BO1_NATIVE_ARCH_FLAGS_PLAIN};-pthread;-D_TIME_BITS=64")

target_compile_definitions(${BIN_NAME} PRIVATE ${BO1_NATIVE_DEFINES})
if (BO1_NATIVE_P32)
    target_compile_options(${BIN_NAME} PRIVATE -include "${NATIVE_DIR}/native_p32.h")
    if (TARGET bo1_d3d9shim)
        target_compile_options(bo1_d3d9shim PRIVATE -include "${NATIVE_DIR}/native_p32.h")
    endif()
endif()

target_include_directories(${BIN_NAME} BEFORE PRIVATE "${WEB_DIR}/compat")
if (BO1_NATIVE_RENDERER)
    target_include_directories(${BIN_NAME} PRIVATE "${WEB_DIR}/d3d9/include")
endif()
target_include_directories(${BIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SRC_DIR}"
    "${SRC_DIR}/libs"
    "${SRC_DIR}/libs/libtomcrypt-1.17/src/headers"
    "${SRC_DIR}/libs/libtommath-1.0"
    "${WEB_DIR}"
    "${NATIVE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"   # <sse2neon/sse2neon.h> (AArch64)
    "${WEB_DIR}/compat/case"
)

set(CMAKE_C_FLAGS_RELEASE "-O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
set(CMAKE_C_FLAGS_DEBUG "-O1 -g")
set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g")

# ----- Link -----
target_link_options(${BIN_NAME} PRIVATE
    ${BO1_NATIVE_ARCH_LINK}
    -pthread
    -stdlib=libc++
    -fuse-ld=lld
    -rdynamic                              # function names in backtrace() output
    "-Wl,--wrap=remove" "-Wl,--wrap=rename"
)
if (NOT BO1_NATIVE_P32)
    # (the 32-bit-pointer build has its own fopen, bo1_stdio.h)
    target_link_options(${BIN_NAME} PRIVATE "-Wl,--wrap=fopen" "-Wl,--wrap=fopen64")
endif()
# SDL2: the window, the GL ES 3 context and input (src/native/native_video.cpp)
if (BO1_NATIVE_ANDROID)
    target_include_directories(${BIN_NAME} PRIVATE "${BO1_ANDROID_SDL2}/include")
    # The compiler runtime first: emulated thread-locals (__emutls_get_address) must be this library's own copy,
    # which allocates with this library's malloc. libSDL2.so exports one too and would win by link order.
    execute_process(COMMAND ${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET} -print-libgcc-file-name
        OUTPUT_VARIABLE BO1_ANDROID_BUILTINS OUTPUT_STRIP_TRAILING_WHITESPACE)
    # libc++ the same way (operator new must be this library's): static, and named before libSDL2.so.
    target_link_libraries(${BIN_NAME} PRIVATE "${BO1_ANDROID_BUILTINS}" c++_static c++abi "${BO1_ANDROID_SDL2}/lib/libSDL2.so"
        GLESv3 EGL log android dl m)
    # (the target stays an "executable" for cmake; -shared above makes the link a library)
    set_target_properties(${BIN_NAME} PROPERTIES OUTPUT_NAME "libbo1" SUFFIX ".so"
        RUNTIME_OUTPUT_DIRECTORY "${BO1_NATIVE_OUTPUT_DIR}" WIN32_EXECUTABLE FALSE)
    add_library(bo1_boot SHARED "${NATIVE_DIR}/android/bo1_boot.c" "${NATIVE_DIR}/android/bo1_driver.c")
    target_include_directories(bo1_boot PRIVATE "${BO1_ANDROID_SDL2}/include")
    target_link_libraries(bo1_boot PRIVATE "${BO1_ANDROID_SDL2}/lib/libSDL2.so" log dl)
    target_link_options(bo1_boot PRIVATE -fuse-ld=lld "-Wl,-z,max-page-size=16384" "-Wl,--no-undefined")
    set_target_properties(bo1_boot PROPERTIES OUTPUT_NAME "main" LIBRARY_OUTPUT_DIRECTORY "${BO1_NATIVE_OUTPUT_DIR}")
    add_executable(bo1_boot_test "${NATIVE_DIR}/android/bo1_boot.c")
    target_compile_definitions(bo1_boot_test PRIVATE BO1_BOOT_TEST)
    target_compile_options(bo1_boot_test PRIVATE -fPIE)
    target_link_libraries(bo1_boot_test PRIVATE log dl)
    target_link_options(bo1_boot_test PRIVATE -fuse-ld=lld -pie)
    set_target_properties(bo1_boot_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BO1_NATIVE_OUTPUT_DIR}")
else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(${BIN_NAME} PRIVATE SDL2::SDL2 GLESv2 dl m)
    set_target_properties(${BIN_NAME} PROPERTIES
        OUTPUT_NAME "bo1"
        RUNTIME_OUTPUT_DIRECTORY "${BO1_NATIVE_OUTPUT_DIR}"
        WIN32_EXECUTABLE FALSE
    )
endif()

message(STATUS "BO1 native build: arch=${BO1_NATIVE_ARCH} renderer=${BO1_NATIVE_RENDERER} -> ${BO1_NATIVE_OUTPUT_DIR}")
