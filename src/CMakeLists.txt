add_executable(Fcitx5
    main.cpp
    windowsfrontend.cpp
    windowssettings.cpp
    windowsinputmethods.cpp
    windowsdictionaries.cpp
)
include("${CMAKE_SOURCE_DIR}/cmake/native-boost.cmake")
add_library(core_i18n STATIC ../win32/i18n/settings.cpp ../win32/i18n/translator.cpp)
target_compile_definitions(core_i18n PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN BOOST_ALL_NO_LIB)
target_link_libraries(core_i18n PUBLIC Boost::locale Boost::property_tree shell32 ole32)
target_link_libraries(Fcitx5 Fcitx5::Core core_i18n advapi32 secur32)

if (ENABLE_WINDOWS_ASCII_FALLBACK)
    target_compile_definitions(Fcitx5 PRIVATE FCITX5_WINDOWS_ASCII_FALLBACK)
endif()

target_include_directories(Fcitx5 PRIVATE "${CMAKE_SOURCE_DIR}/win32/ipc")

install(TARGETS Fcitx5 DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES windowskeyboard.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
