cmake_minimum_required(VERSION 3.21)
project(DownloadManager VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opções comuns a todos os alvos.
add_library(dm_options INTERFACE)
target_include_directories(dm_options INTERFACE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(MSVC)
    target_compile_options(dm_options INTERFACE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(dm_options INTERFACE -Wall -Wextra)
endif()

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

# Núcleo portátil (sem Windows): segmentos, cabeçalhos HTTP, estado de retomada, formatação.
add_library(dm_core STATIC
    src/core/base64.cpp
    src/core/browser_request.cpp
    src/core/command_line.cpp
    src/core/debrid.cpp
    src/core/download_list.cpp
    src/core/format.cpp
    src/core/http_headers.cpp
    src/core/json.cpp
    src/core/rate_limiter.cpp
    src/core/resume_state.cpp
    src/core/rules.cpp
    src/core/segments.cpp
    src/core/settings.cpp
    src/core/update.cpp
    src/core/video.cpp
)
target_link_libraries(dm_core PUBLIC dm_options)

if(MSVC)
    # Runtime estático: os .exe rodam sem o Visual C++ Redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET dm_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32)
    enable_language(RC)

    target_compile_definitions(dm_options INTERFACE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

    # Motor de download (WinHTTP) e textos traduzidos.
    add_library(dm_engine STATIC
        src/engine/debrid_task.cpp
    src/engine/download_task.cpp
        src/engine/http.cpp
        src/engine/output_file.cpp
        src/engine/process.cpp
        src/engine/video_task.cpp
        src/i18n/errors.cpp
        src/i18n/strings.cpp
        src/util/file_io.cpp
        src/util/secure.cpp
        src/util/unicode.cpp
    )
    target_link_libraries(dm_engine PUBLIC dm_core winhttp crypt32 bcrypt)

    # Camada do app (lista de downloads, integrações com o Windows, ferramentas de vídeo).
    add_library(dm_app STATIC
        src/app/browser_integration.cpp
        src/app/debrid_account.cpp
        src/app/download_manager.cpp
        src/app/extractor.cpp
        src/app/organizer.cpp
        src/app/system.cpp
        src/app/updater.cpp
        src/app/video_tools.cpp
    )
    target_link_libraries(dm_app PUBLIC dm_engine shell32 shlwapi ole32 uuid powrprof advapi32)

    add_executable(DownloadManager WIN32
        src/main.cpp
        src/ui/dialogs.cpp
        src/ui/download_list_view.cpp
        src/ui/main_window.cpp
        src/ui/rules_page.cpp
        src/ui/settings_page.cpp
        src/ui/theme.cpp
        src/ui/tray_icon.cpp
        src/ui/video_dialog.cpp
        res/app.rc
    )
    target_include_directories(DownloadManager PRIVATE res)
    target_link_libraries(DownloadManager PRIVATE dm_app comctl32 uxtheme comdlg32 dwmapi gdiplus)

    add_executable(dm-cli
        src/cli/main.cpp
        res/app.rc
    )
    target_link_libraries(dm-cli PRIVATE dm_app)

    # Ponte com a extensão do navegador (Native Messaging). Console: o navegador conversa por stdin/stdout.
    add_executable(dm-host
        src/host/main.cpp
    )
    target_link_libraries(dm-host PRIVATE dm_core)

    # Instalador: o app, a ponte e a extensão vão dentro do setup.exe como recursos RCDATA.
    # DM_FIREFOX_XPI: .xpi assinado pela Mozilla (o CI passa nos releases), registrado no Firefox.
    set(DM_FIREFOX_XPI "" CACHE FILEPATH "Extensão do Firefox assinada (.xpi) para incluir no instalador")
    file(GLOB_RECURSE dm_extension_files RELATIVE ${CMAKE_SOURCE_DIR}/extension CONFIGURE_DEPENDS
         ${CMAKE_SOURCE_DIR}/extension/*)
    # Os .exe são copiados para generated/payload: assim o .rc é recompilado quando eles mudam.
    set(dm_payload_dir ${CMAKE_CURRENT_BINARY_DIR}/generated/payload)
    add_custom_command(
        OUTPUT ${dm_payload_dir}/DownloadManager.exe ${dm_payload_dir}/dm-host.exe
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dm_payload_dir}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:DownloadManager> $<TARGET_FILE:dm-host> ${dm_payload_dir}
        DEPENDS DownloadManager dm-host
        VERBATIM)
    set(dm_payload_names "DownloadManager.exe" "dm-host.exe")
    set(dm_payload_sources ${dm_payload_dir}/DownloadManager.exe ${dm_payload_dir}/dm-host.exe)
    foreach(file ${dm_extension_files})
        list(APPEND dm_payload_names "extension/${file}")
        list(APPEND dm_payload_sources "${CMAKE_SOURCE_DIR}/extension/${file}")
    endforeach()
    if(DM_FIREFOX_XPI)
        list(APPEND dm_payload_names "download-manager.xpi")
        list(APPEND dm_payload_sources "${DM_FIREFOX_XPI}")
    endif()
    set(dm_payload_rc "")
    set(dm_payload_list "")
    set(dm_payload_id 2000)
    foreach(name source IN ZIP_LISTS dm_payload_names dm_payload_sources)
        string(APPEND dm_payload_rc "${dm_payload_id} RCDATA \"${source}\"\n")
        string(APPEND dm_payload_list "    {${dm_payload_id}, L\"${name}\"},\n")
        math(EXPR dm_payload_id "${dm_payload_id} + 1")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/payload.rc.in "${dm_payload_rc}")
    configure_file(${CMAKE_CURRENT_BINARY_DIR}/generated/payload.rc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/payload.rc
                   COPYONLY)
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/payload.h CONTENT
"#pragma once\n// Gerado pelo CMake: arquivos dentro do instalador (id do recurso RCDATA, caminho na pasta do app).\nstruct PayloadFile {\n    int id;\n    const wchar_t* path;\n};\ninline constexpr PayloadFile kPayload[] = {\n${dm_payload_list}};\n")
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/generated/payload.rc PROPERTIES
        OBJECT_DEPENDS "${dm_payload_sources}")

    add_executable(DownloadManager-Setup WIN32
        src/setup/main.cpp
        res/setup.rc
        ${CMAKE_CURRENT_BINARY_DIR}/generated/payload.rc
        ${dm_payload_dir}/DownloadManager.exe
        ${dm_payload_dir}/dm-host.exe
    )
    target_include_directories(DownloadManager-Setup PRIVATE res)
    target_link_libraries(DownloadManager-Setup PRIVATE dm_app comctl32)

    foreach(app DownloadManager DownloadManager-Setup dm-cli dm-host)
        if(MSVC)
            set_property(TARGET ${app} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            # O manifesto vem do app.rc.
            target_link_options(${app} PRIVATE /MANIFEST:NO)
        else()
            if(NOT app STREQUAL "dm-host")
                target_compile_options(${app} PRIVATE -municode)
                target_link_options(${app} PRIVATE -municode)
            endif()
            target_link_options(${app} PRIVATE -static -s)
        endif()
    endforeach()
    foreach(lib dm_engine dm_app)
        if(MSVC)
            set_property(TARGET ${lib} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE dm_core)
    if(MSVC)
        set_property(TARGET core_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(WIN32)
        target_link_options(core_tests PRIVATE -static)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(core_tests PRIVATE Threads::Threads)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME core_tests COMMAND core_tests)
    endif()
endif()
