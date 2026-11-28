cmake_minimum_required(VERSION 3.24)
project(windows_hangul_dependencies VERSION 0.2.0 LANGUAGES C)

# Build libhangul 0.2.0 from the SHA256-pinned release archive with built-in
# keyboard layouts (no expat dependency) and install its hanja dictionaries
# and pkg-config metadata for fcitx5-hangul.
include(FetchContent)
FetchContent_Declare(libhangul
    URL https://github.com/libhangul/libhangul/releases/download/libhangul-0.2.0/libhangul-0.2.0.tar.gz
    URL_HASH SHA256=ea04e6a0cf4840a2a3b5641c1761068c78691036db839d0838f4e7a6553a5120
    SOURCE_SUBDIR windows-source-only)
FetchContent_MakeAvailable(libhangul)

find_package(Git REQUIRED)
set(compatibility_patch "${CMAKE_CURRENT_LIST_DIR}/../../patches/libhangul-windows.patch")
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --no-index --unsafe-paths
    "--directory=${libhangul_SOURCE_DIR}" --reverse --check
    "${compatibility_patch}" WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE patched OUTPUT_QUIET ERROR_QUIET)
if(NOT patched EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --no-index --unsafe-paths
        "--directory=${libhangul_SOURCE_DIR}" "${compatibility_patch}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.." COMMAND_ERROR_IS_FATAL ANY)
endif()

configure_file("${CMAKE_CURRENT_LIST_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

add_library(hangul SHARED
    "${libhangul_SOURCE_DIR}/hangul/hangulctype.c"
    "${libhangul_SOURCE_DIR}/hangul/hangulinputcontext.c"
    "${libhangul_SOURCE_DIR}/hangul/hangulkeyboard.c"
    "${libhangul_SOURCE_DIR}/hangul/hanja.c")
target_compile_definitions(hangul PRIVATE
    HAVE_CONFIG_H=1
    LOCALEDIR="${CMAKE_INSTALL_PREFIX}/share/locale"
    LIBHANGUL_DEFAULT_HANJA_DIC="${CMAKE_INSTALL_PREFIX}/share/libhangul/hanja/hanja.txt"
    LIBHANGUL_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/libhangul"
    TOP_SRCDIR="${libhangul_SOURCE_DIR}")
target_compile_options(hangul PRIVATE
    -Wno-deprecated-declarations
    -Wno-unused-variable
    -Wno-unused-function)
target_include_directories(hangul PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${libhangul_SOURCE_DIR}/hangul")
# Give the shared dependency DLL a distinct runtime name (libhangul-1.dll) so
# it does not collide with fcitx5-hangul's lib/fcitx5/libhangul.dll addon DLL.
set_target_properties(hangul PROPERTIES
    RUNTIME_OUTPUT_NAME "hangul-1"
    VERSION "1.1.0"
    SOVERSION "1"
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

install(TARGETS hangul RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(FILES "${libhangul_SOURCE_DIR}/hangul/hangul.h"
    DESTINATION include/hangul-1.0)
install(FILES
    "${libhangul_SOURCE_DIR}/data/hanja/hanja.txt"
    "${libhangul_SOURCE_DIR}/data/hanja/mssymbol.txt"
    DESTINATION share/libhangul/hanja)
install(FILES "${libhangul_SOURCE_DIR}/COPYING" DESTINATION licenses/libhangul)

set(prefix "${CMAKE_INSTALL_PREFIX}")
set(exec_prefix "${prefix}")
set(libdir "${prefix}/lib")
set(includedir "${prefix}/include")
set(PACKAGE_VERSION "${PROJECT_VERSION}")
configure_file("${libhangul_SOURCE_DIR}/libhangul.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libhangul.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libhangul.pc" DESTINATION lib/pkgconfig)
