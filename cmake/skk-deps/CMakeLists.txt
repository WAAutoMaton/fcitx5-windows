cmake_minimum_required(VERSION 3.24)
project(windows_skk_dependencies LANGUAGES C CXX)

# Build libskk 1.1.1 from the release archive's pre-generated C sources
# without requiring valac or libxkbcommon, and fetch the pinned SKK-JISYO.L
# dictionary for the Windows plugin bundle.
include(FetchContent)
FetchContent_Declare(libskk
    URL https://github.com/ueno/libskk/releases/download/1.1.1/libskk-1.1.1.tar.xz
    URL_HASH SHA256=2d4c15976d459ef0004ce1c0e47506c7859d0b6f4fb104ed837f3326d235bb56
    SOURCE_SUBDIR windows-source-only)
FetchContent_MakeAvailable(libskk)

find_package(Git REQUIRED)
set(compatibility_patch "${CMAKE_CURRENT_LIST_DIR}/../../patches/libskk-windows.patch")
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --no-index --unsafe-paths
    "--directory=${libskk_SOURCE_DIR}" --reverse --check
    "${compatibility_patch}" WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE patched OUTPUT_QUIET ERROR_QUIET)
if(NOT patched EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --no-index --unsafe-paths
        "--directory=${libskk_SOURCE_DIR}" "${compatibility_patch}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.." COMMAND_ERROR_IS_FATAL ANY)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSKK_DEPS REQUIRED IMPORTED_TARGET
    glib-2.0 gobject-2.0 gio-2.0 gee-0.8 json-glib-1.0)
find_package(Fcitx5Utils REQUIRED)

set(VERSION "1.1.1")
set(SKK_API_VERSION "1.0")
configure_file("${CMAKE_CURRENT_LIST_DIR}/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

set(libskk_modules
    rom-kana
    encoding
    dict
    file-dict
    cdb-dict
    user-dict
    skkserv
    key-event
    key-event-filter
    keymap
    rule
    map-file
    state
    context
    candidate
    candidate-list
    nicola
    expr
    util
    keysyms
    completion)

set(libskk_sources)
foreach(name IN LISTS libskk_modules)
    list(APPEND libskk_sources "${libskk_SOURCE_DIR}/libskk/${name}.c")
endforeach()
list(APPEND libskk_sources "${CMAKE_CURRENT_LIST_DIR}/xkbcommon_shim.cpp")

add_library(skk SHARED ${libskk_sources})
target_compile_features(skk PRIVATE cxx_std_20)
target_compile_definitions(skk PRIVATE
    PKGDATADIR="${CMAKE_INSTALL_PREFIX}/share/libskk"
    G_LOG_DOMAIN="libskk")
target_compile_options(skk PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_BINARY_DIR}/config.h;-Wno-deprecated-declarations;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function>)
target_include_directories(skk PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${libskk_SOURCE_DIR}"
    "${libskk_SOURCE_DIR}/libskk")
target_link_libraries(skk PRIVATE PkgConfig::LIBSKK_DEPS Fcitx5::Utils)
set_target_properties(skk PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

install(TARGETS skk RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(FILES "${libskk_SOURCE_DIR}/libskk/libskk.h"
    DESTINATION "include/skk-${SKK_API_VERSION}/libskk")
install(DIRECTORY "${libskk_SOURCE_DIR}/rules"
    DESTINATION share/libskk
    PATTERN "Makefile*" EXCLUDE)
install(FILES "${libskk_SOURCE_DIR}/COPYING" DESTINATION licenses/libskk)

set(prefix "${CMAKE_INSTALL_PREFIX}")
set(exec_prefix "${prefix}")
set(libdir "${prefix}/lib")
set(includedir "${prefix}/include")
configure_file("${libskk_SOURCE_DIR}/libskk/libskk.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libskk.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libskk.pc" DESTINATION lib/pkgconfig)
