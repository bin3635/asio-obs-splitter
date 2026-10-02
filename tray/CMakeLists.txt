add_executable(AsioSplitterTray WIN32
    main.cpp
)

target_include_directories(AsioSplitterTray PRIVATE
    ../common/include
)

target_link_libraries(AsioSplitterTray PRIVATE
    splitter_common
    ole32
    oleaut32
    advapi32
    shell32
    user32
)

target_compile_definitions(AsioSplitterTray PRIVATE
    WINDOWS=1
    WIN32=1
    _WINDOWS
)

set_target_properties(AsioSplitterTray PROPERTIES
    OUTPUT_NAME "AsioSplitterTray"
    PREFIX ""
)
