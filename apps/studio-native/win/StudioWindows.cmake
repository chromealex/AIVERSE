# Windows shell of the HTML5 Studio: Win32 window + WebView2 UI + Vulkan Scene / Game views (win/Main.cpp)
# around the shared StudioHost. Included from apps/studio-native/CMakeLists.txt.
#   Output: AiverseStudio.exe + WebView2Loader.dll + studio-web/ (the built UI) in one folder.
#   Needs:  the Vulkan backend (AIVERSE_VULKAN, on by default on Windows), the Microsoft Edge WebView2 Runtime
#           (part of Windows 11; Windows 10: https://go.microsoft.com/fwlink/p/?LinkId=2124703) and a Vulkan driver.
if(NOT TARGET aiverse_gfx_vulkan)
    message(FATAL_ERROR "The Windows Studio needs the Vulkan backend: configure with -DAIVERSE_VULKAN=ON")
endif()
include(FetchContent)

# WebView2 SDK: headers + WebView2Loader.dll from the NuGet package (loaded at run time, so MSVC and MinGW link alike).
set(AIVERSE_WEBVIEW2_VERSION "1.0.3485.44" CACHE STRING "Microsoft.Web.WebView2 NuGet package version")
set(AIVERSE_WEBVIEW2_DIR "" CACHE PATH "Unpacked Microsoft.Web.WebView2 package (empty: download it from NuGet)")
if(NOT AIVERSE_WEBVIEW2_DIR)
    set(_aiverse_wv2_hash "")
    if(AIVERSE_WEBVIEW2_VERSION STREQUAL "1.0.3485.44")
        set(_aiverse_wv2_hash URL_HASH SHA256=bc09150b179246ac90189649b13be8e6b11b3ac200e817e18df106e1f3cf489e)
    endif()
    FetchContent_Declare(aiverse_webview2
        URL "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${AIVERSE_WEBVIEW2_VERSION}/microsoft.web.webview2.${AIVERSE_WEBVIEW2_VERSION}.nupkg"
        ${_aiverse_wv2_hash}
        DOWNLOAD_NAME webview2.zip # a .nupkg is a zip; the extension tells CMake to unpack it
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(aiverse_webview2)
    set(AIVERSE_WEBVIEW2_DIR "${aiverse_webview2_SOURCE_DIR}")
endif()
if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(_aiverse_wv2_arch arm64)
else()
    set(_aiverse_wv2_arch x64)
endif()
set(_aiverse_wv2_loader "${AIVERSE_WEBVIEW2_DIR}/build/native/${_aiverse_wv2_arch}/WebView2Loader.dll")
if(NOT EXISTS "${AIVERSE_WEBVIEW2_DIR}/build/native/include/WebView2.h" OR NOT EXISTS "${_aiverse_wv2_loader}")
    message(FATAL_ERROR "WebView2 SDK not found in ${AIVERSE_WEBVIEW2_DIR}")
endif()

# SQLite for the asset GUID index: MSVC uses Windows' own winsqlite3 (Windows 10+); other compilers build the
# amalgamation (AIVERSE_SQLITE_AMALGAMATION_DIR, or downloaded from sqlite.org).
if(MSVC)
    add_library(aiverse_studio_sqlite INTERFACE)
    target_compile_definitions(aiverse_studio_sqlite INTERFACE AIVERSE_WINSQLITE=1)
    target_link_libraries(aiverse_studio_sqlite INTERFACE winsqlite3)
else()
    set(AIVERSE_SQLITE_AMALGAMATION_DIR "" CACHE PATH "Folder with sqlite3.c and sqlite3.h (empty: download from sqlite.org)")
    if(NOT AIVERSE_SQLITE_AMALGAMATION_DIR)
        FetchContent_Declare(aiverse_sqlite
            URL "https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(aiverse_sqlite)
        set(AIVERSE_SQLITE_AMALGAMATION_DIR "${aiverse_sqlite_SOURCE_DIR}")
    endif()
    add_library(aiverse_studio_sqlite STATIC "${AIVERSE_SQLITE_AMALGAMATION_DIR}/sqlite3.c")
    target_include_directories(aiverse_studio_sqlite SYSTEM PUBLIC "${AIVERSE_SQLITE_AMALGAMATION_DIR}")
    target_compile_definitions(aiverse_studio_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1 SQLITE_DQS=0)
    if(NOT MSVC)
        target_compile_options(aiverse_studio_sqlite PRIVATE -w)
    endif()
endif()
add_library(SQLite::SQLite3 ALIAS aiverse_studio_sqlite)

if(MSVC)
    set(_aiverse_studio_manifest win/AiverseStudio.manifest) # merged into the linker's manifest
else()
    set(_aiverse_studio_manifest win/AiverseStudio.rc)       # RT_MANIFEST resource for MinGW
endif()
add_executable(aiverse_studio WIN32 win/Main.cpp ${AIVERSE_STUDIO_SOURCES} ${_aiverse_studio_manifest} win/AiverseStudio.icon.rc)
target_include_directories(aiverse_studio PRIVATE "${PROJECT_SOURCE_DIR}")
target_include_directories(aiverse_studio SYSTEM PRIVATE "${AIVERSE_WEBVIEW2_DIR}/build/native/include")
if(NOT MSVC)
    target_include_directories(aiverse_studio SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/win/mingw") # EventToken.h
endif()
target_link_libraries(aiverse_studio PRIVATE aiverse_hosted_client SQLite::SQLite3 aiverse_lua aiverse_editor_core aiverse_runtime
    aiverse_fx aiverse_game_view aiverse_gfx_vulkan AIVERSE::vivid_human AIVERSE::libraries
    ole32 oleaut32 uuid shell32 shlwapi user32 advapi32 dwmapi)
target_sources(aiverse_studio PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_SOURCE_DIR="${PROJECT_SOURCE_DIR}" _WIN32_WINNT=0x0A00)
if(MSVC)
    target_compile_options(aiverse_studio PRIVATE /utf-8 /bigobj)
else()
    target_compile_options(aiverse_studio PRIVATE -Wa,-mbig-obj)
endif()
set_target_properties(aiverse_studio PROPERTIES OUTPUT_NAME AiverseStudio)
# Publish / Build panel: the packager built next to Studio.
if(TARGET aiversebuild)
    add_dependencies(aiverse_studio aiversebuild)
    target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_BUILD_TOOL="$<TARGET_FILE:aiversebuild>")
endif()
# Agent panel: the MCP server the in-editor Claude Code run talks to.
if(TARGET aiversemcp)
    add_dependencies(aiverse_studio aiversemcp)
    target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_MCP_TOOL="$<TARGET_FILE:aiversemcp>")
endif()
# The UI and the WebView2 loader next to the exe; runs every build so UI-only changes are picked up.
add_custom_target(aiverse_studio_web_assets
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${AIVERSE_STUDIO_WEB_DIST}" "$<TARGET_FILE_DIR:aiverse_studio>/studio-web"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_aiverse_wv2_loader}" "$<TARGET_FILE_DIR:aiverse_studio>"
    VERBATIM)
add_dependencies(aiverse_studio aiverse_studio_web_assets)

# Studio session contract (the same test macOS runs after every Studio build).
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_studio_session_tests tests/StudioSessionTests.cpp StudioSession.cpp StudioAssetCatalog.cpp StudioAssetTransfers.cpp)
    target_include_directories(aiverse_studio_session_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_session_tests PRIVATE aiverse_lua aiverse_editor_core SQLite::SQLite3 aiverse_vivid_human aiverse_modeling aiverse_models aiverse_zstd_dec)
    if(MSVC)
        target_compile_options(aiverse_studio_session_tests PRIVATE /utf-8 /bigobj)
    else()
        target_compile_options(aiverse_studio_session_tests PRIVATE -Wa,-mbig-obj)
    endif()
    add_test(NAME studio_session COMMAND aiverse_studio_session_tests)
    set_tests_properties(studio_session PROPERTIES LABELS "EDITOR")
endif()
