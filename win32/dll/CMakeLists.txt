configure_file(resources.rc.in resources.rc @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
    PROPERTIES OBJECT_DEPENDS "${PROJECT_BINARY_DIR}/dll/penguin.ico")

add_library(${TSF_TARGET} SHARED
    main.cpp
    register.cpp
    util.cpp
    exports.def
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
)
add_dependencies(${TSF_TARGET} GenerateIco)
target_include_directories(${TSF_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_options(${TSF_TARGET} PRIVATE "-Wno-dll-attribute-on-redeclaration")

if(TSF_ARCH STREQUAL "x86")
    # CTF TIP profile/category keys are shared between WOW64 views. The x64
    # DLL owns that metadata; x86 only owns its redirected COM registration.
    target_compile_definitions(${TSF_TARGET} PRIVATE FCITX5_TSF_X86)
endif()

target_link_libraries(${TSF_TARGET} tsf)

option(ENABLE_MODERN_TSF_CATEGORIES "Declare UIElement/immersive support for explicit system acceptance" OFF)
if(ENABLE_MODERN_TSF_CATEGORIES)
    target_compile_definitions(${TSF_TARGET} PRIVATE FCITX5_MODERN_TSF_CATEGORIES)
endif()
