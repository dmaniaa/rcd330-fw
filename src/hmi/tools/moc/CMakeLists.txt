cmake_minimum_required(VERSION 3.20)

# Standalone host build of Qt 4.7.4 moc (output revision 62) + POSIX bootstrap
# subset, invoked by tools/build_hmi.sh.
project(rcd330_host_moc LANGUAGES CXX)

if(NOT RCD330_QT474_SRC_DIR OR NOT IS_DIRECTORY "${RCD330_QT474_SRC_DIR}/src/tools/moc")
    message(FATAL_ERROR "RCD330_QT474_SRC_DIR must point to extracted Qt 4.7.4 moc sources")
endif()

set(_host_cfg_dir "${CMAKE_BINARY_DIR}/qt_host_config")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/host_qconfig.h"
    "${_host_cfg_dir}/QtCore/qconfig.h" COPYONLY)

if(APPLE)
    # Under QWS + Q_WS_QWS, only qstring.cpp (CFStringCompare) and the dead
    # Mac OS X 10.5 fallback in qfsfileengine.cpp include <private/qcore_mac_p.h>.
    # Provide a CoreFoundation-only header + MacintoshVersion >= MV_10_6 stub
    # so deprecated Carbon/CoreServices APIs and qcore_mac.cpp are avoided.
    file(WRITE "${_host_cfg_dir}/private/qcore_mac_p.h"
        "#ifndef RCD330_HOST_QCORE_MAC_P_H\n"
        "#define RCD330_HOST_QCORE_MAC_P_H\n"
        "#include <CoreFoundation/CoreFoundation.h>\n"
        "#include \"qstring.h\"\n"
        "struct FSRef { unsigned char hidden[80]; };\n"
        "static inline int FSPathMakeRef(const UInt8 *, FSRef *, Boolean *) { return -1; }\n"
        "struct QCFString {\n"
        "    static inline QString toQString(CFStringRef) { return QString(); }\n"
        "};\n"
        "#ifndef noErr\n"
        "#  define noErr 0\n"
        "#endif\n"
        "#endif\n")
    file(WRITE "${CMAKE_BINARY_DIR}/host_mac_stub.cpp"
        "#include <QtCore/qglobal.h>\n"
        "QT_BEGIN_NAMESPACE\n"
        "const QSysInfo::MacVersion QSysInfo::MacintoshVersion = QSysInfo::MV_SNOWLEOPARD;\n"
        "QT_END_NAMESPACE\n")
endif()

# src/tools/moc/main.cpp includes ../../corelib/global/qconfig.cpp (normally
# emitted by Qt configure); an empty stub satisfies it.
set(_qconfig_cpp "${RCD330_QT474_SRC_DIR}/src/corelib/global/qconfig.cpp")
if(NOT EXISTS "${_qconfig_cpp}")
    file(WRITE "${_qconfig_cpp}" "/* generated stub for host moc */\n")
endif()

find_package(ZLIB REQUIRED)

set(_qt "${RCD330_QT474_SRC_DIR}")
set(_moc_sources
    "${_qt}/src/tools/moc/main.cpp"
    "${_qt}/src/tools/moc/moc.cpp"
    "${_qt}/src/tools/moc/preprocessor.cpp"
    "${_qt}/src/tools/moc/generator.cpp"
    "${_qt}/src/tools/moc/parser.cpp"
    "${_qt}/src/tools/moc/token.cpp"
    "${_qt}/src/corelib/global/qglobal.cpp"
    "${_qt}/src/corelib/global/qmalloc.cpp"
    "${_qt}/src/corelib/global/qnumeric.cpp"
    "${_qt}/src/corelib/io/qabstractfileengine.cpp"
    "${_qt}/src/corelib/io/qbuffer.cpp"
    "${_qt}/src/corelib/io/qdatastream.cpp"
    "${_qt}/src/corelib/io/qdir.cpp"
    "${_qt}/src/corelib/io/qdiriterator.cpp"
    "${_qt}/src/corelib/io/qfile.cpp"
    "${_qt}/src/corelib/io/qfileinfo.cpp"
    "${_qt}/src/corelib/io/qfsfileengine.cpp"
    "${_qt}/src/corelib/io/qfsfileengine_iterator.cpp"
    "${_qt}/src/corelib/io/qfsfileengine_unix.cpp"
    "${_qt}/src/corelib/io/qfsfileengine_iterator_unix.cpp"
    "${_qt}/src/corelib/io/qiodevice.cpp"
    "${_qt}/src/corelib/io/qtemporaryfile.cpp"
    "${_qt}/src/corelib/io/qtextstream.cpp"
    "${_qt}/src/corelib/io/qurl.cpp"
    "${_qt}/src/corelib/kernel/qmetatype.cpp"
    "${_qt}/src/corelib/kernel/qvariant.cpp"
    "${_qt}/src/corelib/tools/qbitarray.cpp"
    "${_qt}/src/corelib/tools/qbytearray.cpp"
    "${_qt}/src/corelib/tools/qbytearraymatcher.cpp"
    "${_qt}/src/corelib/tools/qdatetime.cpp"
    "${_qt}/src/corelib/tools/qhash.cpp"
    "${_qt}/src/corelib/tools/qlist.cpp"
    "${_qt}/src/corelib/tools/qlocale.cpp"
    "${_qt}/src/corelib/tools/qmap.cpp"
    "${_qt}/src/corelib/tools/qregexp.cpp"
    "${_qt}/src/corelib/tools/qstring.cpp"
    "${_qt}/src/corelib/tools/qstringlist.cpp"
    "${_qt}/src/corelib/tools/qvector.cpp"
    "${_qt}/src/corelib/tools/qvsnprintf.cpp"
    "${_qt}/src/corelib/codecs/qtextcodec.cpp"
    "${_qt}/src/corelib/codecs/qlatincodec.cpp"
    "${_qt}/src/corelib/codecs/qutfcodec.cpp"
    "${_qt}/src/corelib/codecs/qisciicodec.cpp"
    "${_qt}/src/corelib/codecs/qsimplecodec.cpp"
    "${_qt}/src/corelib/codecs/qtsciicodec.cpp")

if(APPLE)
    list(APPEND _moc_sources "${CMAKE_BINARY_DIR}/host_mac_stub.cpp")
endif()

add_executable(moc ${_moc_sources})

if(APPLE)
    set(_mkspec_dir "${_qt}/mkspecs/macx-g++")
else()
    set(_mkspec_dir "${_qt}/mkspecs/linux-g++")
endif()

# _host_cfg_dir must precede _qt/include and _qt/include/QtCore so our
# QtCore/qconfig.h (and private/qcore_mac_p.h on APPLE) wins.
target_include_directories(moc PRIVATE
    "${_host_cfg_dir}"
    "${_qt}/include"
    "${_qt}/include/QtCore"
    "${_qt}/src/tools/moc"
    "${_qt}/src/corelib/global"
    "${_qt}/src/corelib/tools"
    "${_qt}/src/corelib/io"
    "${_qt}/src/corelib/codecs"
    "${_qt}/src/corelib/kernel"
    "${_mkspec_dir}")

target_compile_definitions(moc PRIVATE
    QT_BOOTSTRAPPED
    QT_MOC
    QT_LITE_UNICODE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_CODECS
    QT_NO_DATASTREAM
    QT_NO_GEOM_VARIANT
    QT_NO_LIBRARY
    QT_NO_QOBJECT
    QT_NO_STL
    QT_NO_SYSTEMLOCALE
    QT_NO_TEXTSTREAM
    QT_NO_THREAD
    QT_NO_UNICODETABLES
    QT_NO_USING_NAMESPACE)

target_compile_options(moc PRIVATE
    -std=gnu++98
    -Wno-deprecated-declarations
    -Wno-register
    "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c++11-narrowing>"
    "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-#warnings>"
    "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-logical-not-parentheses>")

target_link_libraries(moc PRIVATE ZLIB::ZLIB)
if(APPLE)
    target_link_libraries(moc PRIVATE "-framework CoreFoundation")
endif()
