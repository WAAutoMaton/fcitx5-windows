cmake_minimum_required(VERSION 3.27)

# Keep the in-process TIP independent of runtimes already loaded by its host.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()

project(fcitx5-win32 VERSION 0.1.0)
enable_language(RC)

set(TSF_ARCH "" CACHE STRING "TSF target architecture (x64 or x86)")
set_property(CACHE TSF_ARCH PROPERTY STRINGS x64 x86)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#if !defined(_M_X64) && !defined(__x86_64__)
#error Expected x64 compiler
#endif
int main() { static_assert(sizeof(void*) == 8); }" TSF_COMPILER_X64)
check_cxx_source_compiles("
#if !defined(_M_IX86) && !defined(__i386__)
#error Expected x86 compiler
#endif
int main() { static_assert(sizeof(void*) == 4); }" TSF_COMPILER_X86)
if(TSF_COMPILER_X64)
    set(detected_tsf_arch x64)
elseif(TSF_COMPILER_X86)
    set(detected_tsf_arch x86)
else()
    message(FATAL_ERROR "Only x64 and x86 TSF targets are supported")
endif()
if(TSF_ARCH AND NOT TSF_ARCH STREQUAL detected_tsf_arch)
    message(FATAL_ERROR "TSF_ARCH=${TSF_ARCH} disagrees with compiler target ${detected_tsf_arch}; use a separate build tree and matching VS environment")
endif()
set(TSF_ARCH "${detected_tsf_arch}" CACHE STRING "TSF target architecture (x64 or x86)" FORCE)
if(TSF_ARCH STREQUAL "x86")
    set(TSF_TARGET fcitx5-x86)
else()
    set(TSF_TARGET fcitx5-x86_64)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
   AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"
   AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C,CXX>:-fms-runtime-lib=static$<$<CONFIG:Debug>:_dbg>>")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/native-boost.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/windows-translations.cmake")
add_subdirectory(i18n)
add_library(windows_diagnostic STATIC ipc/diagnostic_log.cpp)
target_compile_definitions(windows_diagnostic PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(windows_diagnostic PUBLIC shell32 ole32)
execute_process(COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    OUTPUT_VARIABLE diagnostic_build OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
target_compile_definitions(windows_diagnostic PRIVATE
    FCITX5_DIAGNOSTIC_BUILD="${diagnostic_build}-${CMAKE_BUILD_TYPE}")

add_subdirectory(assets)
add_subdirectory(tsf)
add_subdirectory(dll)
add_subdirectory(setup)

enable_testing()
add_subdirectory(tests)
