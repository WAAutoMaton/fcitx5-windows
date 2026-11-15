cmake_minimum_required(VERSION 3.24)
project(windows_zhuyin_dependencies LANGUAGES CXX)

# libpinyin's legacy CMake build does not build libzhuyin. Use the source
# lists from its autotools build, with the native Windows Berkeley DB backend.
include(FetchContent)
FetchContent_Declare(libpinyin
    URL https://codeload.github.com/libpinyin/libpinyin/tar.gz/refs/tags/2.10.3
    URL_HASH SHA256=a49286721fb2b0234d86c095db9226246b0aa4a0bb6a885d0902da2743c56476
    SOURCE_SUBDIR windows-source-only)
FetchContent_MakeAvailable(libpinyin)
find_package(Git REQUIRED)
set(compatibility_patch "${CMAKE_CURRENT_LIST_DIR}/../../patches/libpinyin-windows.patch")
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --no-index --unsafe-paths
    "--directory=${libpinyin_SOURCE_DIR}" --reverse --check
    "${compatibility_patch}" WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE patched OUTPUT_QUIET ERROR_QUIET)
if(NOT patched EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --no-index --unsafe-paths
        "--directory=${libpinyin_SOURCE_DIR}" "${compatibility_patch}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.." COMMAND_ERROR_IS_FATAL ANY)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
find_path(DB_INCLUDE_DIR db.h REQUIRED)
find_library(DB_LIBRARY NAMES db REQUIRED)
set(HAVE_BERKELEY_DB 1)
configure_file("${libpinyin_SOURCE_DIR}/config.h.cmake" config.h)

set(storage_sources
    phrase_index phrase_large_table2 phrase_large_table3 ngram tag_utility
    chewing_key pinyin_parser2 zhuyin_parser2 phonetic_key_matrix
    chewing_large_table chewing_large_table2 table_info punct_table
    ngram_bdb phrase_large_table3_bdb chewing_large_table2_bdb punct_table_bdb)
set(sources)
foreach(name IN LISTS storage_sources)
    list(APPEND sources "${libpinyin_SOURCE_DIR}/src/storage/${name}.cpp")
endforeach()
foreach(name pinyin_lookup2 phrase_lookup lookup phonetic_lookup)
    list(APPEND sources "${libpinyin_SOURCE_DIR}/src/lookup/${name}.cpp")
endforeach()
add_library(zhuyin-storage STATIC ${sources})
target_compile_features(zhuyin-storage PUBLIC cxx_std_11)
target_compile_definitions(zhuyin-storage PUBLIC HAVE_CONFIG_H)
# The upstream generators use assert() expressions for required work.
target_compile_options(zhuyin-storage PUBLIC -UNDEBUG)
target_include_directories(zhuyin-storage PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}" "${DB_INCLUDE_DIR}"
    "${libpinyin_SOURCE_DIR}/src" "${libpinyin_SOURCE_DIR}/src/include"
    "${libpinyin_SOURCE_DIR}/src/storage" "${libpinyin_SOURCE_DIR}/src/lookup"
    "${libpinyin_SOURCE_DIR}/utils")
target_link_libraries(zhuyin-storage PUBLIC PkgConfig::GLIB "${DB_LIBRARY}")

add_library(zhuyin SHARED "${libpinyin_SOURCE_DIR}/src/zhuyin.cpp")
target_link_libraries(zhuyin PRIVATE zhuyin-storage)
set_target_properties(zhuyin PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_library(zhuyin-internal STATIC "${libpinyin_SOURCE_DIR}/src/pinyin_internal.cpp")
target_link_libraries(zhuyin-internal PUBLIC zhuyin-storage)
foreach(name gen_binary_files import_interpolation)
    add_executable(${name} "${libpinyin_SOURCE_DIR}/utils/storage/${name}.cpp")
    target_link_libraries(${name} PRIVATE zhuyin-internal)
endforeach()
add_executable(gen_unigram "${libpinyin_SOURCE_DIR}/utils/training/gen_unigram.cpp")
target_link_libraries(gen_unigram PRIVATE zhuyin-internal)
add_executable(zhuyin_model_probe test_model.cpp)
target_link_libraries(zhuyin_model_probe PRIVATE zhuyin zhuyin-storage)
install(TARGETS zhuyin gen_binary_files import_interpolation gen_unigram
    RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(FILES "${libpinyin_SOURCE_DIR}/src/zhuyin.h"
    "${libpinyin_SOURCE_DIR}/src/storage/pinyin_custom2.h"
    "${libpinyin_SOURCE_DIR}/src/storage/zhuyin_custom2.h"
    "${libpinyin_SOURCE_DIR}/src/include/novel_types.h"
    DESTINATION include/libpinyin-2.10.3)
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(exec_prefix "${prefix}")
set(libdir "${prefix}/lib")
set(includedir "${prefix}/include")
set(VERSION 2.10.3)
set(LIBPINYIN_BINARY_VERSION 15.0)
set(DATABASE_FORMAT BerkeleyDB)
configure_file("${libpinyin_SOURCE_DIR}/libzhuyin.pc.in" libzhuyin.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libzhuyin.pc" DESTINATION lib/pkgconfig)
install(FILES "${libpinyin_SOURCE_DIR}/COPYING" DESTINATION licenses/libpinyin)
