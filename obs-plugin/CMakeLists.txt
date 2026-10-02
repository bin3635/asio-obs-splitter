cmake_minimum_required(VERSION 3.20)
project(obs-asio-splitter LANGUAGES CXX C)

include(FetchContent)

# Git 저장소 대신 소스 아카이브를 직접 다운로드 (서브모듈 충돌 및 다운로드 지연 원천 차단)
FetchContent_Declare(
    obs-studio-headers
    URL https://github.com/obsproject/obs-studio/archive/refs/tags/30.2.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(obs-studio-headers)
if(NOT obs-studio-headers_POPULATED)
    FetchContent_Populate(obs-studio-headers)
endif()

# 플러그인 모듈 정의
add_library(obs-asio-splitter MODULE
    src/plugin_main.cpp
    src/ipc_receiver.cpp
    src/ipc_receiver.h
)

target_compile_features(obs-asio-splitter PRIVATE cxx_std_20)

# 헤더 경로 지정
target_include_directories(obs-asio-splitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/common/include
    ${obs-studio-headers_SOURCE_DIR}/libobs
)

# obs.lib 경로 지정
target_link_directories(obs-asio-splitter PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/obs-studio/lib
)

# 링크
target_link_libraries(obs-asio-splitter PRIVATE
    obs
)

if(MSVC)
    target_compile_definitions(obs-asio-splitter PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
