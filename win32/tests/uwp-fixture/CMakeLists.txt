find_path(UWP_WINRT_INCLUDE winrt/Windows.UI.Xaml.h
    HINTS "$ENV{WindowsSdkDir}/Include/$ENV{WindowsSDKVersion}/cppwinrt" REQUIRED)
find_program(MAKEAPPX_EXECUTABLE makeappx REQUIRED)
find_program(FIXTURE_MAGICK_EXECUTABLE magick REQUIRED)
add_executable(uwp_fixture WIN32 EXCLUDE_FROM_ALL main.cpp)
set_target_properties(uwp_fixture PROPERTIES OUTPUT_NAME Fcitx5ModernInputFixture)
target_include_directories(uwp_fixture PRIVATE "${UWP_WINRT_INCLUDE}")
target_compile_definitions(uwp_fixture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(uwp_fixture PRIVATE windowsapp)
set(package_directory "${CMAKE_CURRENT_BINARY_DIR}/package")
add_custom_target(uwp_fixture_package
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${package_directory}/Assets"
    COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:uwp_fixture> "${package_directory}/Fcitx5ModernInputFixture.exe"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/AppxManifest.xml" "${package_directory}/AppxManifest.xml"
    COMMAND "${FIXTURE_MAGICK_EXECUTABLE}" -background none "${CMAKE_SOURCE_DIR}/assets/penguin.svg"
        -resize 50x50 -gravity center -extent 50x50 "${package_directory}/Assets/StoreLogo.png"
    COMMAND "${FIXTURE_MAGICK_EXECUTABLE}" -background none "${CMAKE_SOURCE_DIR}/assets/penguin.svg"
        -resize 44x44 -gravity center -extent 44x44 "${package_directory}/Assets/Square44x44Logo.png"
    COMMAND "${FIXTURE_MAGICK_EXECUTABLE}" -background none "${CMAKE_SOURCE_DIR}/assets/penguin.svg"
        -resize 150x150 -gravity center -extent 150x150 "${package_directory}/Assets/Square150x150Logo.png"
    COMMAND "${MAKEAPPX_EXECUTABLE}" pack /o /d "${package_directory}"
        /p "${CMAKE_CURRENT_BINARY_DIR}/Fcitx5ModernInputFixture.appx"
    DEPENDS uwp_fixture VERBATIM)
