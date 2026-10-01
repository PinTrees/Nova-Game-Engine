@echo off
setlocal
echo ===================================================
echo   NOVA Game Engine - Automated CMake Build System
echo ===================================================

rem Usage: build.bat          = Debug (with /O2, D3D debug layer, unoptimized shaders)
rem        build.bat release  = Release (optimized engine + shaders, no D3D debug layer)
set CONFIG=Debug
if /I "%~1"=="release" set CONFIG=Release

set CMAKE_PATH="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist %CMAKE_PATH% (
    echo [ERROR] CMake executable not found at %CMAKE_PATH%
    pause
    exit /b 1
)

echo [1/2] Configuring CMake Project...
%CMAKE_PATH% -B build -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configure failed!
    exit /b %ERRORLEVEL%
)

rem Debug and Release both write Binaries\ and Packages\*\Plugins\: after switching the config the old
rem outputs can look up to date and are not relinked, so remove them (keep this file ASCII + CRLF for cmd)
set LASTCFG=
if exist build\last_config.txt set /p LASTCFG=<build\last_config.txt
if /I not "%LASTCFG%"=="%CONFIG%" (
    if exist Binaries\NovaCore.dll erase Binaries\NovaCore.dll
    if exist Binaries\NovaEngine.exe erase Binaries\NovaEngine.exe
    for /d %%P in (Packages\*) do if exist "%%P\Plugins\*.dll" erase "%%P\Plugins\*.dll"
)

echo [2/2] Compiling Engine (%CONFIG% x64)...
%CMAKE_PATH% --build build --config %CONFIG% -j
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed!
    exit /b %ERRORLEVEL%
)

>build\last_config.txt echo %CONFIG%
echo ===================================================
echo   Build Successful! (%CONFIG%) Output: Binaries\NovaEngine.exe
echo ===================================================
endlocal
