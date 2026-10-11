find_program(WINDOWS_MSGFMT NAMES msgfmt
    HINTS "C:/msys64/clang64/bin" "C:/msys64/usr/bin" REQUIRED)
set(WINDOWS_LOCALE_OUTPUT "${CMAKE_BINARY_DIR}/share/locale" CACHE PATH "Windows translation output")
foreach(language zh_CN zh_TW)
    set(output "${WINDOWS_LOCALE_OUTPUT}/${language}/LC_MESSAGES/fcitx5-windows.mo")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WINDOWS_LOCALE_OUTPUT}/${language}/LC_MESSAGES"
        COMMAND "${WINDOWS_MSGFMT}" --check -o "${output}" "${CMAKE_CURRENT_LIST_DIR}/../po/${language}.po"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../po/${language}.po" VERBATIM)
    list(APPEND windows_translation_outputs "${output}")
    install(FILES "${output}" DESTINATION "share/locale/${language}/LC_MESSAGES")
endforeach()
add_custom_target(windows_translations ALL DEPENDS ${windows_translation_outputs})
