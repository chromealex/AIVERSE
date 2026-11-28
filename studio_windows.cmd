@echo off
rem AIVERSE Studio on Windows: builds the web UI and the native Studio (WebView2 + Vulkan), then opens it.
rem Usage: studio_windows.cmd [project folder]   (omit to reopen the last project)
rem Needs: Visual Studio 2022 (Desktop C++), CMake, Node.js; at run time the WebView2 Runtime and a Vulkan driver.
setlocal
set "PROJECT_ABS=%~f1"
cd /d "%~dp0" || exit /b 1
where node >nul 2>nul || (echo Node.js is missing: https://nodejs.org & exit /b 1)
where cmake >nul 2>nul || (echo CMake is missing: https://cmake.org/download & exit /b 1)
echo == web
pushd apps\studio-web
if not exist node_modules call npm install --no-audit --no-fund || (popd & echo WEB BUILD FAILED & exit /b 1)
call npx vite build || (popd & echo WEB BUILD FAILED & exit /b 1)
popd
echo == configure
cmake -S . -B build\windows -G "Visual Studio 17 2022" -A x64 -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=ON "-DAIVERSE_STUDIO_WEB_DIST=%CD%\apps\studio-web\dist" || (echo CONFIGURE FAILED & exit /b 1)
echo == build
cmake --build build\windows --config Release --target aiverse_studio --parallel || (echo BUILD FAILED & exit /b 1)
set "EXE=build\windows\apps\studio-native\Release\AiverseStudio.exe"
echo == open %EXE%
if defined PROJECT_ABS (
    start "" "%EXE%" --project "%PROJECT_ABS%" || exit /b 1
) else (
    start "" "%EXE%" || exit /b 1
)
echo == done
