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
target_link_libraries(Fcitx5SetupHelper PRIVATE advapi32 secur32 user32 shell32 ole32 oleaut32 uuid rstrtmgr)
