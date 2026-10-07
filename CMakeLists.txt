cmake_minimum_required(VERSION 3.21)
project(DownloadManager VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

add_executable(DownloadManager WIN32
    src/main.cpp
    src/i18n/strings.cpp
    src/ui/main_window.cpp
    res/app.rc
)

target_include_directories(DownloadManager PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(DownloadManager PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(DownloadManager PRIVATE comctl32)
set_target_properties(DownloadManager PROPERTIES OUTPUT_NAME "DownloadManager")

if(MSVC)
    # Runtime estático: o .exe roda sem o Visual C++ Redistributable.
    set_property(TARGET DownloadManager PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(DownloadManager PRIVATE /W4 /permissive- /utf-8 /EHsc)
    # O manifesto vem do app.rc.
    target_link_options(DownloadManager PRIVATE /MANIFEST:NO)
else()
    target_compile_options(DownloadManager PRIVATE -Wall -Wextra -municode)
    target_link_options(DownloadManager PRIVATE -municode -static -s)
endif()
