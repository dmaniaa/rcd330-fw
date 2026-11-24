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
