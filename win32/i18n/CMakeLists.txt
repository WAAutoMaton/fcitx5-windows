if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.27)
    project(fcitx5-settings-i18n LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/native-boost.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/windows-translations.cmake")
endif()

add_library(windows_i18n STATIC settings.cpp translator.cpp monitor.cpp)
target_compile_definitions(windows_i18n PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN BOOST_ALL_NO_LIB)
target_link_libraries(windows_i18n PUBLIC Boost::locale Boost::property_tree shell32 ole32)
target_include_directories(windows_i18n PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(merged_libraries "$<TARGET_FILE:windows_i18n>")
    foreach(library boost_locale boost_thread boost_chrono boost_atomic boost_date_time)
        if(TARGET ${library})
            list(APPEND merged_libraries "$<TARGET_FILE:${library}>")
        endif()
    endforeach()
    add_custom_command(TARGET windows_i18n POST_BUILD
        COMMAND lib.exe /NOLOGO "/OUT:${CMAKE_BINARY_DIR}/settings-i18n.lib"
            ${merged_libraries}
        VERBATIM)
endif()
