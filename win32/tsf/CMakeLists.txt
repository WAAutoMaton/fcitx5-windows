add_library(tsf STATIC
    tsf.cpp
    TextInputProcessorEx.cpp
    ThreadMgrEventSink.cpp
    TextEditSink.cpp
    KeyEventSink.cpp
    EditSession.cpp
    CompositionSink.cpp
    pipeclient.cpp
    candidatewindow.cpp
    pollstate.cpp
    DisplayAttribute.cpp
)
target_compile_definitions(tsf PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(tsf PRIVATE "${CMAKE_SOURCE_DIR}/ipc")
target_link_libraries(tsf PUBLIC advapi32 user32 gdi32 ole32 oleaut32 uuid)
