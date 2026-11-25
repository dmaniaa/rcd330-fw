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

add_library(rcd330_vmf_client SHARED IMPORTED)
set_target_properties(rcd330_vmf_client PROPERTIES
    IMPORTED_LOCATION "${RCD330_SYSROOT}/opt/lib/libvmf_client.so")

add_library(rcd330_ts SHARED IMPORTED)
set_target_properties(rcd330_ts PROPERTIES
    IMPORTED_LOCATION "${RCD330_SYSROOT}/usr/lib/libts-1.0.so.0")

# Host-built Qt 4.7.4 moc (output revision 62) so Q_OBJECT code avoids Qt 4.8+
# symbols absent from the device libQtCore.so.4.
if(NOT RCD330_MOC_EXECUTABLE OR NOT EXISTS "${RCD330_MOC_EXECUTABLE}")
    message(FATAL_ERROR "RCD330_MOC_EXECUTABLE must point to host Qt 4.7.4 moc; configure via tools/build_hmi.sh")
endif()

function(hmi_wrap_cpp out_var)
    set(_moc_dir "${CMAKE_CURRENT_BINARY_DIR}/moc")
    file(MAKE_DIRECTORY "${_moc_dir}")
    set(_out "${${out_var}}")
    foreach(_hdr IN LISTS ARGN)
        get_filename_component(_abs_hdr "${_hdr}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        file(RELATIVE_PATH _rel_hdr "${CMAKE_CURRENT_SOURCE_DIR}" "${_abs_hdr}")
        string(REGEX REPLACE "\\.[^.]+$" "" _rel_stem "${_rel_hdr}")
        string(REPLACE "/" "_" _rel_flat "${_rel_stem}")
        set(_moc_cpp "${_moc_dir}/moc_${_rel_flat}.cpp")
        add_custom_command(
            OUTPUT "${_moc_cpp}"
            COMMAND "${RCD330_MOC_EXECUTABLE}"
                -DQT_NO_DEBUG
                "-I${RCD330_QT_CONFIG_DIR}"
                "-I${RCD330_QT474_DIR}/include"
                "-I${CMAKE_CURRENT_SOURCE_DIR}"
                "${_abs_hdr}"
                -o "${_moc_cpp}"
            DEPENDS "${_abs_hdr}" "${RCD330_MOC_EXECUTABLE}"
            COMMENT "Running Qt 4.7.4 moc on ${_rel_hdr}"
            VERBATIM)
        list(APPEND _out "${_moc_cpp}")
    endforeach()
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

# Tables generated from stock .rodata by tools/build_hmi.sh (src/hmi/tools).
if(NOT RCD330_GEN_DIR OR NOT EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp"
   OR NOT EXISTS "${RCD330_GEN_DIR}/strings_vw.cpp")
    message(FATAL_ERROR "RCD330_GEN_DIR must hold igdt_tables_vw.cpp and strings_vw.cpp; configure via tools/build_hmi.sh")
endif()

set(HMI_MOC_SOURCES)
hmi_wrap_cpp(HMI_MOC_SOURCES
    service/utility/vbasetask.h
    control_logic/vcontroltask.h
    control_logic/subsystem/pss/vpssretrytimer.h
    control_logic/input/vkeydriver.h
    control_logic/input/vknobdriver.h
    display/multitouch/vmultitouch.h
    display/igdt/extend/igdt_wrapper.h
    display/controls/controlbase/itembase.h
    display/controls/controlbase/complexcontrol.h
    display/controls/controlbase/buttonbase.h
    display/controls/controlbase/igdtbuttonbase.h
    display/controls/controlparts/igdtimageitem.h
    display/controls/controlparts/igdttextitem.h
    display/controls/controlparts/compositeitem.h
    display/controls/controlparts/igdtbutton.h
    display/page/base/basepage.h
    display/page/base/pagemanager.h
    display/page/appconnect/vappnoconnection.h
    display/page/appconnect/vappconnectinfo.h
    display/page/tuner/vtunermainpage.h
    display/vdisplaytask.h)

add_executable(hmi
    main.cpp
    service/utility/vbasetask.cpp
    service/utility/vbaseproxy.cpp
    service/vmf/vmf_frame.cpp
    service/vmf/VmfThread.cpp
    control_logic/subsystem/proxy_decoders.cpp
    control_logic/subsystem/phase3_stubs.cpp
    control_logic/subsystem/power/vpowerproxy.cpp
    control_logic/subsystem/display/vdisplayproxy.cpp
    control_logic/subsystem/appconnect/vappconnectproxy.cpp
    control_logic/subsystem/audio/vaudioproxy.cpp
    control_logic/subsystem/bap/vbapproxy.cpp
    control_logic/subsystem/amfm/vamfmproxy.cpp
    control_logic/subsystem/media/vmediaproxy.cpp
    control_logic/subsystem/system_time_calendar/vsystemtimecalendarproxy.cpp
    control_logic/subsystem/pss/vpssproxy.cpp
    control_logic/subsystem/pss/vpssretrytimer.cpp
    control_logic/subsystem/diag/vdiagproxy.cpp
    control_logic/mode/vbasefilter.cpp
    control_logic/mode/vprehandlefilter.cpp
    control_logic/mode/audio/vaudionotifyfilter.cpp
    control_logic/mode/camera/vcameranotifyfilter.cpp
    control_logic/mode/display/vdisplaycmdfilter.cpp
    control_logic/mode/ipcl/vhmikey.cpp
    control_logic/mode/ipcl/vipclnotifyfilter.cpp
    control_logic/mode/media/vmedianotifyfilter.cpp
    control_logic/mode/power/vpowernotifyfilter.cpp
    control_logic/vcontroltask.cpp
    control_logic/input/input_decoders.cpp
    control_logic/input/vkeydriver.cpp
    control_logic/input/vknobdriver.cpp
    display/multitouch/vmultitouch.cpp
    display/base/control_layout.cpp
    display/base/controlcreator.cpp
    display/controls/controlbase/vmoveablebase.cpp
    display/controls/controlbase/vvisibilitybase.cpp
    display/controls/controlbase/vdynamicbase.cpp
    display/controls/controlbase/itembase.cpp
    display/controls/controlbase/complexcontrol.cpp
    display/controls/controlbase/buttonbase.cpp
    display/controls/controlbase/igdtbuttonbase.cpp
    display/controls/controlparts/igdtimageitem.cpp
    display/controls/controlparts/igdttextitem.cpp
    display/controls/controlparts/compositeitem.cpp
    display/controls/controlparts/compositebackground.cpp
    display/controls/controlparts/igdtbutton.cpp
    display/controls/controlparts/vmainpagestatusline.cpp
    display/page/base/basepage.cpp
    display/page/base/pagemanager.cpp
    display/page/appconnect/vappnoconnection.cpp
    display/page/appconnect/vappconnectinfo.cpp
    display/page/tuner/vtunermainpage.cpp
    display/vdisplaytask.cpp
    ${HMI_MOC_SOURCES}
    display/igdt/igdt_strings.cpp
    display/igdt/igdt_ovg.cpp
    display/igdt/igdt_gl.cpp
    display/igdt/igdt_runtime.cpp
    display/igdt/igdt_scene.cpp
    display/igdt/igdt_text.cpp
    display/igdt/igdt_textbuf.cpp
    display/igdt/igdt_touch.cpp
    display/igdt/extend/igdt_wrapper.cpp
    "${RCD330_GEN_DIR}/igdt_tables_vw.cpp"
    "${RCD330_GEN_DIR}/strings_vw.cpp")
target_include_directories(hmi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hmi PRIVATE rcd330_qtcore rcd330_osal rcd330_GAL rcd330_EGL rcd330_GLESv2
    rcd330_freetype rcd330_vmf_client rcd330_ts pthread)
target_compile_options(hmi PRIVATE -Wall -Wextra)
