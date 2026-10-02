add_library(ProxyAsio SHARED
    src/dllmain.cpp
    src/proxy_asio.cpp
    src/proxy_asio.h
    src/ipc_sender.cpp
    src/ipc_sender.h
    proxy_asio.def
)

target_include_directories(ProxyAsio PRIVATE
    include
    src
)

target_link_libraries(ProxyAsio PRIVATE
    splitter_common
    ole32
    oleaut32
    advapi32
    user32
)

target_compile_definitions(ProxyAsio PRIVATE
    WINDOWS=1
    WIN32=1
    _WINDOWS
)

set_target_properties(ProxyAsio PROPERTIES
    OUTPUT_NAME "ProxyAsio64"
    PREFIX ""
)
