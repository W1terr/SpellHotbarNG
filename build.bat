@echo off
rem Builds SpellHotbarNG.dll
rem Usage: build.bat [releasedbg|debug]   (default: releasedbg)
rem Output folder: SHNG_BUILD_DIR (default "build"), can be set in build.local.bat (not in git)
setlocal
cd /d "%~dp0"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=releasedbg"

if exist "%~dp0build.local.bat" call "%~dp0build.local.bat"
if not defined SHNG_BUILD_DIR set "SHNG_BUILD_DIR=build"

rem A stale VCPKG_ROOT makes xmake fail with "vcpkg not found"
set "VCPKG_ROOT="
set "PATH=C:\Program Files\xmake;%PATH%"

xmake f -p windows -a x64 -m %MODE% -o "%SHNG_BUILD_DIR%" -y || exit /b 1
xmake build -y || exit /b 1
xmake project -k compile_commands >nul
