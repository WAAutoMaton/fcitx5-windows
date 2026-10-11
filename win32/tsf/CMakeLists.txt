set(buildinfo_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(tsf_buildinfo
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT_DIR=${buildinfo_dir}"
        -P "${CMAKE_SOURCE_DIR}/cmake/buildinfo.cmake"
    BYPRODUCTS "${buildinfo_dir}/buildinfo.h"
    VERBATIM)

add_library(tsf STATIC
    tsf.cpp
    TextInputProcessorEx.cpp
    ThreadMgrEventSink.cpp
    TextEditSink.cpp
    KeyEventSink.cpp
    compartment.cpp
    EditSession.cpp
    CompositionSink.cpp
    pipeclient.cpp
    candidatewindow.cpp
    langbaritem.cpp
    aboutwindow.cpp
    pollstate.cpp
    servicecontrol.cpp
    DisplayAttribute.cpp
)
add_dependencies(tsf tsf_buildinfo)
target_include_directories(tsf PRIVATE "${buildinfo_dir}")
target_compile_definitions(tsf PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(tsf PRIVATE "${CMAKE_SOURCE_DIR}/ipc")
target_link_libraries(tsf PUBLIC advapi32 secur32 user32 gdi32 ole32 oleaut32 uuid shell32 comctl32 d2d1 dwrite windowscodecs)
