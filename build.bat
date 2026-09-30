@echo off
setlocal
echo ===================================================
echo   DX11 Game Engine - Automated CMake Build System
echo ===================================================

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

echo [2/2] Compiling Engine (Debug x64)...
%CMAKE_PATH% --build build --config Debug -j
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed!
    exit /b %ERRORLEVEL%
)

echo ===================================================
echo   Build Successful! Output: Binaries\DX11.exe
echo ===================================================
endlocal
