@echo off
setlocal enabledelayedexpansion

rem ImageEditor build script.
rem   build.bat                  configure (if needed) + build Release
rem   build.bat <vcpkg-static>   same, with a different vcpkg x64-windows-static
rem   build.bat <vcpkg> <builddir>  same, into another build directory
rem
rem Output: <builddir>\bin\Release\ImageEditor.exe

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "VCPKG_STATIC=E:\Programs\vcpkg\installed\x64-windows-static"
if not "%~1"=="" set "VCPKG_STATIC=%~1"
set "BUILD=%ROOT%\build"
if not "%~2"=="" set "BUILD=%~2"

if not exist "%VCPKG_STATIC%\lib\vips.lib" (
    echo [error] vcpkg static triplet not found: %VCPKG_STATIC%
    echo         pass it as the first argument, e.g.
    echo         build.bat D:\vcpkg\installed\x64-windows-static
    exit /b 2
)

set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
    set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if not exist "!CMAKE!" set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if not exist "!CMAKE!" set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if not exist "!CMAKE!" (
        echo [error] cmake not found. Install Visual Studio 2022 with the C++ CMake tools,
        echo         or put cmake.exe on PATH.
        exit /b 2
    )
)

if not exist "%BUILD%\CMakeCache.txt" (
    echo [configure] "%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64
    "%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DVCPKG_STATIC="%VCPKG_STATIC%"
    if errorlevel 1 (
        echo [error] configure failed
        exit /b 1
    )
)

echo [build] Release
"%CMAKE%" --build "%BUILD%" --config Release
if errorlevel 1 (
    echo [error] build failed
    exit /b 1
)

set "EXE=%BUILD%\bin\Release\ImageEditor.exe"
if not exist "%EXE%" (
    echo [error] output missing: %EXE%
    exit /b 1
)
for %%A in ("%EXE%") do echo [ok] %%A  %%~zA bytes
exit /b 0
