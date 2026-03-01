@echo off
REM P2P Node Build Script for Windows (Visual Studio)

setlocal enabledelayedexpansion

echo [*] P2P Node Build Script (Windows)
echo.

REM Check for CMake
where cmake >nul 2>nul
if errorlevel 1 (
    echo [!] CMake not found. Please install CMake 3.10+
    exit /b 1
)

for /f "tokens=*" %%i in ('cmake --version ^| findstr /R "cmake version"') do set CMAKE_VERSION=%%i
echo [+] Found: %CMAKE_VERSION%

REM Detect Visual Studio
set VS_VERSION=
for %%v in (16 17 15) do (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set VS_VERSION=2019
        goto :found_vs
    )
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set VS_VERSION=2022
        goto :found_vs
    )
)

if not defined VS_VERSION (
    echo [!] Visual Studio not found
    echo [*] Using MinGW instead
    set GENERATOR=MinGW Makefiles
    goto :build_setup
)

:found_vs
echo [+] Found Visual Studio %VS_VERSION%
set GENERATOR=Visual Studio 16 2019
if %VS_VERSION% == 2022 set GENERATOR=Visual Studio 17 2022

:build_setup
REM Create build directory
if not exist build mkdir build
echo [+] Created/Using build directory

cd build

REM Configure
echo [+] Configuring CMake with generator: %GENERATOR%...
cmake .. -G "%GENERATOR%"
if errorlevel 1 goto :error

REM Build
echo [+] Building in Release configuration...
cmake --build . --config Release
if errorlevel 1 goto :error

cd ..

echo.
echo [+] Build completed successfully!
echo.
echo Binary location: build\bin\Release\p2p_node.exe
echo.
echo To run the node:
echo   build\bin\Release\p2p_node.exe [port] [storage_path]
echo.
echo Example:
echo   build\bin\Release\p2p_node.exe 6881 ./storage

goto :eof

:error
echo.
echo [!] Build failed!
exit /b 1
