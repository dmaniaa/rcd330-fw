cmake_minimum_required(VERSION 3.20)

# Recreated opt/hmi (firmware 5624). Configure via tools/build_hmi.sh, which
# fetches the third-party headers and passes cmake/zig-armv7-toolchain.cmake.
project(rcd330_hmi LANGUAGES C CXX)

# Our qconfig.h must win over the Qt tree's forwarder for <QtCore/qconfig.h>.
set(RCD330_QT_CONFIG_DIR "${CMAKE_BINARY_DIR}/qt_config")
configure_file(third_party/qt/qconfig.h "${RCD330_QT_CONFIG_DIR}/QtCore/qconfig.h" COPYONLY)

add_library(rcd330_qtcore SHARED IMPORTED)
set_target_properties(rcd330_qtcore PROPERTIES
    IMPORTED_LOCATION "${RCD330_SYSROOT}/usr/lib/libQtCore.so.4")
target_include_directories(rcd330_qtcore SYSTEM INTERFACE
    "${RCD330_QT_CONFIG_DIR}"
    "${RCD330_QT474_DIR}/include")
# Release Qt usage like stock: without QT_NO_DEBUG, SIGNAL()/SLOT() call
# qFlagLocation(), which no stock binary imports.
target_compile_definitions(rcd330_qtcore INTERFACE QT_NO_DEBUG)

add_library(rcd330_osal SHARED IMPORTED)
set_target_properties(rcd330_osal PROPERTIES
    IMPORTED_LOCATION "${RCD330_SYSROOT}/opt/lib/libOSAL.so.1")

# Vivante GPU stack, linked by soname like stock (DT_NEEDED libGAL.so,
# libEGL.so.1, libGLESv2.so.2). The real libraries, not the QEMU shim that
# the staged rootfs installs as libEGL.so.1 / libGLESv2.so.2.
foreach(lib GAL:libGAL-fb.so EGL:libEGL-fb.so GLESv2:libGLESv2-fb.so.2.0.0)
    string(REPLACE ":" ";" parts "${lib}")
    list(GET parts 0 name)
    list(GET parts 1 file)
    add_library(rcd330_${name} SHARED IMPORTED)
    set_target_properties(rcd330_${name} PROPERTIES IMPORTED_LOCATION "${RCD330_SYSROOT}/usr/lib/${file}")
endforeach()

# FreeType: the device libfreetype.so.6 (2.4.8), compiled against the
# matching 2.4.8 headers fetched by tools/build_hmi.sh.
add_library(rcd330_freetype SHARED IMPORTED)
set_target_properties(rcd330_freetype PROPERTIES
    IMPORTED_LOCATION "${RCD330_SYSROOT}/usr/lib/libfreetype.so.6")
target_include_directories(rcd330_freetype SYSTEM INTERFACE "${RCD330_FT248_DIR}/include")

# Tables generated from stock .rodata by tools/build_hmi.sh (src/hmi/tools).
if(NOT RCD330_GEN_DIR OR NOT EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp"
   OR NOT EXISTS "${RCD330_GEN_DIR}/strings_vw.cpp")
    message(FATAL_ERROR "RCD330_GEN_DIR must hold igdt_tables_vw.cpp and strings_vw.cpp; configure via tools/build_hmi.sh")
endif()

add_executable(hmi
    main.cpp
    display/igdt/igdt_strings.cpp
    display/igdt/igdt_ovg.cpp
    display/igdt/igdt_gl.cpp
    display/igdt/igdt_scene.cpp
    display/igdt/igdt_text.cpp
    display/igdt/igdt_textbuf.cpp
    display/igdt/extend/igdt_wrapper.cpp
    "${RCD330_GEN_DIR}/igdt_tables_vw.cpp"
    "${RCD330_GEN_DIR}/strings_vw.cpp")
target_include_directories(hmi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hmi PRIVATE rcd330_qtcore rcd330_osal rcd330_GAL rcd330_EGL rcd330_GLESv2
    rcd330_freetype)
target_compile_options(hmi PRIVATE -Wall -Wextra)
