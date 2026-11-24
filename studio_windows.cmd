@echo off
rem AIVERSE Studio on Windows: builds the web UI and the native Studio (WebView2 + Vulkan), then opens it.
rem Usage: studio_windows.cmd [project folder]   (default: the last opened project / templates\skirmish)
rem Needs: Visual Studio 2022 (Desktop C++), CMake, Node.js; at run time the WebView2 Runtime and a Vulkan driver.
setlocal
cd /d "%~dp0"
set "PROJECT=%~1"
if "%PROJECT%"=="" set "PROJECT=templates\skirmish"
where node >nul 2>nul || (echo Node.js is missing: https://nodejs.org & exit /b 1)
where cmake >nul 2>nul || (echo CMake is missing: https://cmake.org/download & exit /b 1)
echo == web
pushd apps\studio-web
if not exist node_modules call npm install --no-audit --no-fund || (popd & echo WEB BUILD FAILED & exit /b 1)
call npx vite build || (popd & echo WEB BUILD FAILED & exit /b 1)
popd
echo == configure
cmake -S . -B build\windows -G "Visual Studio 17 2022" -A x64 -DAIVERSE_BUILD_STUDIO_HTML5=ON || (echo CONFIGURE FAILED & exit /b 1)
echo == build
cmake --build build\windows --config Release --target aiverse_studio --parallel || (echo BUILD FAILED & exit /b 1)
set "EXE=build\windows\apps\studio-native\Release\AiverseStudio.exe"
echo == open %EXE%
for %%I in ("%PROJECT%") do set "PROJECT_ABS=%%~fI"
start "" "%EXE%" --project "%PROJECT_ABS%"
echo == done
