add_library(tsf STATIC
    tsf.cpp
    TextInputProcessorEx.cpp
    ThreadMgrEventSink.cpp
    TextEditSink.cpp
    KeyEventSink.cpp
    EditSession.cpp
    CompositionSink.cpp
    pipeclient.cpp
)

target_include_directories(tsf PRIVATE "${CMAKE_SOURCE_DIR}/ipc")
