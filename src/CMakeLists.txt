add_executable(Fcitx5
    main.cpp
    windowsfrontend.cpp
)
target_link_libraries(Fcitx5 Fcitx5::Core advapi32)

if (ENABLE_WINDOWS_ASCII_FALLBACK)
    target_compile_definitions(Fcitx5 PRIVATE FCITX5_WINDOWS_ASCII_FALLBACK)
endif()

target_include_directories(Fcitx5 PRIVATE "${CMAKE_SOURCE_DIR}/win32/ipc")

install(TARGETS Fcitx5 DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES windowskeyboard.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
