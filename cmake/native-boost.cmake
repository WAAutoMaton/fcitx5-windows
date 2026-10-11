include(FetchContent)
set(NATIVE_BOOST_HEADERS "" CACHE PATH "Boost 1.92 portable headers")
if(NOT NATIVE_BOOST_HEADERS)
    foreach(prefix "${WINDOWS_DEPENDENCY_PREFIX}" "${CMAKE_CURRENT_LIST_DIR}/../build/deps/clang64" "${CMAKE_SYSROOT}" "$ENV{MSYSTEM_PREFIX}" "C:/msys64/clang64")
        if(EXISTS "${prefix}/include/boost/version.hpp")
            set(NATIVE_BOOST_HEADERS "${prefix}/include")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${NATIVE_BOOST_HEADERS}/boost/version.hpp")
    message(FATAL_ERROR "Provide Boost 1.92 headers with -DNATIVE_BOOST_HEADERS=<include directory>")
endif()
file(STRINGS "${NATIVE_BOOST_HEADERS}/boost/version.hpp" boost_version REGEX "^#define BOOST_VERSION ")
if(NOT boost_version STREQUAL "#define BOOST_VERSION 109200")
    message(FATAL_ERROR "Native Boost.Locale requires matching Boost 1.92 headers")
endif()
# Copy only portable Boost headers, never a GNU sysroot include directory.
file(COPY "${NATIVE_BOOST_HEADERS}/boost" DESTINATION "${CMAKE_BINARY_DIR}/native-headers")
FetchContent_Declare(native_boost
    URL https://codeload.github.com/boostorg/locale/tar.gz/refs/tags/boost-1.92.0
    URL_HASH SHA256=78d1091559daf640703b6717a0743c305c7a66d0f38ec7f5ff5b60f42b7d5d9c
    TIMEOUT 180 INACTIVITY_TIMEOUT 30 DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR fcitx5-messages-only)
FetchContent_MakeAvailable(native_boost)
# Explicit message facets need no global backend or Boost.Thread initialization.
add_library(boost_locale STATIC
    "${native_boost_SOURCE_DIR}/src/shared/message.cpp"
    "${native_boost_SOURCE_DIR}/src/shared/mo_lambda.cpp"
    "${native_boost_SOURCE_DIR}/src/shared/ids.cpp"
    "${native_boost_SOURCE_DIR}/src/encoding/codepage.cpp"
    "${native_boost_SOURCE_DIR}/src/util/encoding.cpp"
    "${native_boost_SOURCE_DIR}/src/util/locale_data.cpp")
add_library(Boost::locale ALIAS boost_locale)
target_include_directories(boost_locale PUBLIC
    "${native_boost_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}/native-headers")
target_compile_definitions(boost_locale PUBLIC BOOST_ALL_NO_LIB BOOST_LOCALE_STATIC_LINK
    PRIVATE BOOST_LOCALE_SOURCE BOOST_LOCALE_WITH_WINAPI=1 NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_library(boost_property_tree INTERFACE)
add_library(Boost::property_tree ALIAS boost_property_tree)
target_include_directories(boost_property_tree INTERFACE "${CMAKE_BINARY_DIR}/native-headers")
install(FILES "${native_boost_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME Boost-1.92-LICENSE.txt)
