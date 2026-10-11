cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()

project(fcitx5-win32 VERSION 0.1.0)
enable_language(RC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/native-boost.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/windows-translations.cmake")
add_subdirectory(i18n)

add_subdirectory(assets)
add_subdirectory(tsf)
add_subdirectory(dll)
add_subdirectory(setup)

enable_testing()
add_subdirectory(tests)
