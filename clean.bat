@echo off
echo ===================================================
echo   Mimic Engine - Cache & Disk Cleanup Tool
echo ===================================================

echo [1/3] Cleaning Visual Studio Intermediate cache...
if exist "Intermediate" (
    rmdir /s /q "Intermediate"
    echo   - Removed Intermediate\
)

echo [2/3] Cleaning CMake build directory...
if exist "build" (
    rmdir /s /q "build"
    echo   - Removed build\
)

echo [3/3] Cleaning temporary compiler outputs (*.obj, *.pch, *.ilk, *.pdb)...
del /s /q /f *.pch >nul 2>&1
del /s /q /f *.ilk >nul 2>&1

echo ===================================================
echo   Cleanup Complete! Re-run build.bat anytime to compile.
echo ===================================================
