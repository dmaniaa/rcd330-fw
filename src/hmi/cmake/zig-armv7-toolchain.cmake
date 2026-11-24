# Cross toolchain for the recreated opt/hmi: ARMv7 Cortex-A9, NEON, softfp,
# glibc 2.13 symbol versions, and the device's GCC 4.6 libstdc++ (6.0.16).
#
# Compiling C++ uses `zig clang` (plain clang, no zig libc++ injection) against
# the GCC 4.6 libstdc++ headers; linking uses the `zig cc` C driver so zig's
# libc++ is never pulled in, and the device libstdc++/libgcc_s are linked
# explicitly from the firmware sysroot.
#
# Required cache variables (tools/build_hmi.sh sets them):
#   RCD330_SYSROOT   staged firmware rootfs (build/cache/<fw_id>/stage)
#   RCD330_GCC46_DIR extracted GCC 4.6 headers (contains c++/4.6)
#   RCD330_QT474_DIR extracted Qt 4.7.4 headers (contains include/QtCore)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7-a)

# try_compile() re-reads this file in a fresh project; forward our variables.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RCD330_SYSROOT RCD330_GCC46_DIR RCD330_QT474_DIR)

foreach(var RCD330_SYSROOT RCD330_GCC46_DIR RCD330_QT474_DIR)
    if(NOT ${var})
        message(FATAL_ERROR "${var} is not set; configure through tools/build_hmi.sh")
    endif()
    if(NOT IS_DIRECTORY "${${var}}")
        message(FATAL_ERROR "${var}=${${var}} does not exist; run tools/build_hmi.sh")
    endif()
endforeach()

find_program(ZIG_EXECUTABLE zig REQUIRED)
# The include order below and the glibc 2.13 header workarounds
# (-U_FILE_OFFSET_BITS/-U_TIME_BITS, zig's lib/libc/include layout) are tied
# to one zig release; another version may silently change the ABI.
set(RCD330_ZIG_VERSION 0.17.1)
execute_process(
    COMMAND "${ZIG_EXECUTABLE}" version
    OUTPUT_VARIABLE _zig_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _zig_version_rc)
if(NOT _zig_version_rc EQUAL 0 OR NOT _zig_version STREQUAL RCD330_ZIG_VERSION)
    message(FATAL_ERROR "zig ${RCD330_ZIG_VERSION} required (found '${_zig_version}' at ${ZIG_EXECUTABLE}); "
        "install that release or update RCD330_ZIG_VERSION here after re-verifying the ABI gate")
endif()
execute_process(
    COMMAND "${ZIG_EXECUTABLE}" env
    OUTPUT_VARIABLE _zig_env
    RESULT_VARIABLE _zig_env_rc)
string(REGEX MATCH "lib_dir[^\"]*\"([^\"]+)\"" _zig_lib_match "${_zig_env}")
set(ZIG_LIB_DIR "${CMAKE_MATCH_1}")
if(NOT _zig_env_rc EQUAL 0 OR NOT IS_DIRECTORY "${ZIG_LIB_DIR}/libc/include/arm-linux-gnu")
    message(FATAL_ERROR "cannot locate zig's bundled glibc headers via `zig env` (got '${ZIG_LIB_DIR}')")
endif()

set(RCD330_ZIG_C_TARGET arm-linux-gnueabi.2.13)
set(RCD330_CPU_FLAGS "-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp")

set(CMAKE_C_COMPILER "${ZIG_EXECUTABLE}")
set(CMAKE_C_COMPILER_ARG1 cc)
# zig cc spells the CPU zig-style (cortex_a9+neon); zig clang takes clang spelling.
set(CMAKE_C_FLAGS_INIT "-target ${RCD330_ZIG_C_TARGET} -mcpu=cortex_a9+neon -mfloat-abi=softfp -U_FILE_OFFSET_BITS -U_TIME_BITS")

# Include order matters: libstdc++'s <cstdlib> etc. #include_next the C headers.
set(_cxx_includes
    "${RCD330_GCC46_DIR}/c++/4.6"
    "${RCD330_GCC46_DIR}/c++/4.6/arm-linux-gnueabi"
    "${ZIG_LIB_DIR}/include"
    "${ZIG_LIB_DIR}/libc/include/arm-linux-gnu"
    "${ZIG_LIB_DIR}/libc/include/generic-glibc"
    "${ZIG_LIB_DIR}/libc/include/arm-linux-any"
    "${ZIG_LIB_DIR}/libc/include/any-linux-any")
set(_cxx_isystem "")
foreach(dir IN LISTS _cxx_includes)
    string(APPEND _cxx_isystem " -isystem \"${dir}\"")
endforeach()

set(CMAKE_CXX_COMPILER "${ZIG_EXECUTABLE}")
set(CMAKE_CXX_COMPILER_ARG1 clang)
set(CMAKE_CXX_FLAGS_INIT
    "-target armv7a-linux-gnueabi ${RCD330_CPU_FLAGS} -std=gnu++98 -nostdinc -nostdinc++${_cxx_isystem} -D__GLIBC_MINOR__=13 -U_FILE_OFFSET_BITS -U_TIME_BITS")

# Link through the zig C driver with the device C++ runtime. --no-undefined
# turns an unresolved Qt/libstdc++ symbol (a qconfig/ABI mismatch) into an error.
# DEVIATION: no -Wl,-rpath-link to the sysroot lib dirs: zig's linker ignores
# it ("rpath-link option is unimplemented"), and lld does not need transitive
# shared-library dependencies to link an executable.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-target ${RCD330_ZIG_C_TARGET} -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--no-undefined -Wl,--as-needed")
# Sysroot library paths must never end up in DT_RUNPATH of the device binary.
# A cache entry: CMakeGenericSystem's set(CACHE) would drop a normal variable.
set(CMAKE_SKIP_RPATH ON CACHE BOOL "No host rpath in device binaries" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES
    "\"${RCD330_SYSROOT}/lib/libstdc++.so.6\" \"${RCD330_SYSROOT}/lib/libgcc_s.so.1\"")
set(CMAKE_CXX_LINK_EXECUTABLE
    "\"${ZIG_EXECUTABLE}\" cc <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

set(CMAKE_FIND_ROOT_PATH "${RCD330_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
