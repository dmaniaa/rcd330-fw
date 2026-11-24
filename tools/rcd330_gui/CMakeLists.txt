cmake_minimum_required(VERSION 3.20)
project(rcd330_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_library(rcd330_gui_core STATIC
    src/rfb_protocol.h
    src/rfb_protocol.cpp
    src/rfb_widget.h
    src/rfb_widget.cpp
    src/vip_client.h
    src/vip_client.cpp
    src/coding_bridge.h
    src/coding_bridge.cpp
    src/bezel_button.h
    src/bezel_button.cpp
    src/rotary_knob_widget.h
    src/rotary_knob_widget.cpp
    src/vehicle_tab.h
    src/vehicle_tab.cpp
    src/coding_tab.h
    src/coding_tab.cpp
    src/log_tab.h
    src/log_tab.cpp
    src/main_window.h
    src/main_window.cpp
)
target_include_directories(rcd330_gui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rcd330_gui_core PUBLIC Qt6::Widgets Qt6::Network)

add_executable(rcd330_gui src/main.cpp)
target_link_libraries(rcd330_gui PRIVATE rcd330_gui_core)

option(BUILD_TESTING "Build Qt6 unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_rcd330_gui tests/test_rcd330_gui.cpp)
    target_compile_definitions(test_rcd330_gui PRIVATE RCD330_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
    target_link_libraries(test_rcd330_gui PRIVATE rcd330_gui_core Qt6::Test)
    add_test(NAME test_rcd330_gui COMMAND test_rcd330_gui)
    set_tests_properties(test_rcd330_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
