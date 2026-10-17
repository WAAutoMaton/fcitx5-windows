add_library(setup_upgrade_files STATIC upgrade_files.cpp)
target_compile_definitions(setup_upgrade_files PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(setup_upgrade_files PUBLIC advapi32 bcrypt)
add_library(setup_upgrade STATIC upgrade.cpp upgrade_system.cpp)
target_link_libraries(setup_upgrade PUBLIC setup_upgrade_files Boost::property_tree
    ole32 oleaut32 shell32 taskschd uuid)

add_executable(Fcitx5SetupHelper WIN32
    main.cpp
    userprofile.cpp
    ../dll/register.cpp
    ../dll/util.cpp
    ../tsf/pipeclient.cpp
)
target_compile_definitions(Fcitx5SetupHelper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(ENABLE_MODERN_TSF_CATEGORIES)
    target_compile_definitions(Fcitx5SetupHelper PRIVATE FCITX5_MODERN_TSF_CATEGORIES)
endif()
target_link_libraries(Fcitx5SetupHelper PRIVATE windows_diagnostic advapi32 secur32 user32 shell32 ole32 oleaut32 uuid rstrtmgr)
target_link_libraries(Fcitx5SetupHelper PRIVATE setup_upgrade)

add_executable(Fcitx5Upgrade WIN32 main.cpp userprofile.cpp
    ../dll/register.cpp ../dll/util.cpp ../tsf/pipeclient.cpp)
target_compile_definitions(Fcitx5Upgrade PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN FCITX5_UPGRADE_HOST)
if(ENABLE_MODERN_TSF_CATEGORIES)
    target_compile_definitions(Fcitx5Upgrade PRIVATE FCITX5_MODERN_TSF_CATEGORIES)
endif()
target_link_libraries(Fcitx5Upgrade PRIVATE setup_upgrade windows_diagnostic
    advapi32 secur32 user32 shell32 ole32 oleaut32 uuid rstrtmgr)
