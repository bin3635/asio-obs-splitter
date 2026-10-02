cmake_minimum_required(VERSION 3.10.0)
project(asio-obs-splitter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(asio-obs-splitter main.cpp)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project requires a 64-bit build.")
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
    add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

add_subdirectory(common)
add_subdirectory(proxy-asio)
add_subdirectory(obs-plugin)
add_subdirectory(tray)
