cmake_minimum_required(VERSION 3.20)

# Host-side unit tests (macOS/Linux compiler, not the zig ARM toolchain) for
# the portable parts of src/hmi: no Qt, GL or EGL. Run through
# `tools/build_hmi.sh --host-tests`.
#
# Adding a test: create test_<name>.cpp here and add one hmi_add_test() line
# below; list the portable sources it needs after the name.
project(rcd330_hmi_host_tests LANGUAGES CXX)
enable_testing()

set(HMI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/..")

function(hmi_add_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    set_target_properties(${name} PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(${name} PRIVATE "${HMI_SRC}")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

hmi_add_test(test_igdt_strings "${HMI_SRC}/display/igdt/igdt_strings.cpp")

# The gresfiles sweep needs the generated tex_table; only available after a
# full tools/build_hmi.sh run (RCD330_GEN_DIR), otherwise it is compiled out.
set(HMI_OVG_SOURCES "${HMI_SRC}/display/igdt/igdt_ovg.cpp")
if(RCD330_GEN_DIR AND EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
    list(APPEND HMI_OVG_SOURCES "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
endif()
hmi_add_test(test_igdt_ovg ${HMI_OVG_SOURCES})
if(RCD330_GEN_DIR AND EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
    target_compile_definitions(test_igdt_ovg PRIVATE HMI_HAVE_IGDT_TABLES)
endif()

hmi_add_test(test_igdt_textbuf "${HMI_SRC}/display/igdt/igdt_textbuf.cpp"
    "${HMI_SRC}/display/igdt/igdt_strings.cpp")

# Runtime checks need the generated scene tables, like test_igdt_ovg.
set(HMI_RUNTIME_SOURCES "${HMI_SRC}/display/igdt/igdt_runtime.cpp")
if(RCD330_GEN_DIR AND EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
    list(APPEND HMI_RUNTIME_SOURCES "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
endif()
hmi_add_test(test_igdt_runtime ${HMI_RUNTIME_SOURCES})
if(RCD330_GEN_DIR AND EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
    target_compile_definitions(test_igdt_runtime PRIVATE HMI_HAVE_IGDT_TABLES)
endif()

# Touch queue / hit test: the hit tests need the generated scene tables.
set(HMI_TOUCH_SOURCES "${HMI_SRC}/display/igdt/igdt_touch.cpp" "${HMI_SRC}/display/igdt/igdt_runtime.cpp")
if(RCD330_GEN_DIR AND EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
    list(APPEND HMI_TOUCH_SOURCES "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
endif()
hmi_add_test(test_igdt_touch ${HMI_TOUCH_SOURCES})
if(RCD330_GEN_DIR AND EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
    target_compile_definitions(test_igdt_touch PRIVATE HMI_HAVE_IGDT_TABLES)
endif()

# VMF framing (VBaseProxy::PackMsg 0x0005fe68) and listener routing (VmfThread).
hmi_add_test(test_vmf_core "${HMI_SRC}/service/vmf/vmf_frame.cpp")

# Subsystem proxy VMF payload decoders and display page tables.
hmi_add_test(test_proxies "${HMI_SRC}/control_logic/subsystem/proxy_decoders.cpp")

# Notify filter chain (control_logic/mode) against a mock IFilterActions.
set(HMI_MODE "${HMI_SRC}/control_logic/mode")
hmi_add_test(test_filters "${HMI_MODE}/vbasefilter.cpp" "${HMI_MODE}/vprehandlefilter.cpp"
    "${HMI_MODE}/ipcl/vhmikey.cpp" "${HMI_MODE}/ipcl/vipclnotifyfilter.cpp"
    "${HMI_MODE}/power/vpowernotifyfilter.cpp" "${HMI_MODE}/display/vdisplaycmdfilter.cpp")
target_compile_definitions(test_filters PRIVATE HMI_HOST_TEST)

# Input driver decoders (VKeyDriver / VKnobDriver / VMultiTouch).
hmi_add_test(test_input_drivers "${HMI_SRC}/control_logic/input/input_decoders.cpp")

# Control library layout (display/base/control_layout): node lookup, rects,
# composite sizing and the button touch state machine. The scene-table
# checks need the generated tables, like test_igdt_touch.
set(HMI_CONTROLS_SOURCES "${HMI_SRC}/display/base/control_layout.cpp" "${HMI_SRC}/display/igdt/igdt_runtime.cpp")
if(RCD330_GEN_DIR AND EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
    list(APPEND HMI_CONTROLS_SOURCES "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
endif()
hmi_add_test(test_controls ${HMI_CONTROLS_SOURCES})
if(RCD330_GEN_DIR AND EXISTS "${RCD330_GEN_DIR}/igdt_tables_vw.cpp")
    target_compile_definitions(test_controls PRIVATE HMI_HAVE_IGDT_TABLES)
endif()
