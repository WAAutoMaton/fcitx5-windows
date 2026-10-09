configure_file(resources.rc.in resources.rc @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
    PROPERTIES OBJECT_DEPENDS "${PROJECT_BINARY_DIR}/dll/penguin.ico")

add_library(fcitx5-x86_64 SHARED
    main.cpp
    register.cpp
    util.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
)
add_dependencies(fcitx5-x86_64 GenerateIco)
target_include_directories(fcitx5-x86_64 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_options(fcitx5-x86_64 PRIVATE "-Wno-dll-attribute-on-redeclaration")

target_link_libraries(fcitx5-x86_64 tsf)
