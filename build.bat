@echo off
rem XY4101 Simulator build script (Windows)
rem Toolchain: MSYS2 mingw32 (i686, 32-bit) + Ninja
rem
rem Usage:
rem   build.bat                        -> Release build (out\xysim.exe)
rem   build.bat debug                  -> Debug build (-g, no opt, out-dbg\xysim.exe)
rem   build.bat clean                  -> Clean both configs
rem   build.bat --sdk D:/path/to/sdk   -> Release build with custom SDK path
rem   build.bat debug --sdk D:/path    -> Debug build with custom SDK path
rem   build.bat --mingw C:/msys64/mingw32/bin   -> override MinGW dir
rem   build.bat -h / --help            -> Show this help
rem
rem SDK path: defaults to ..\LTEcat1_SDK_AP, override with --sdk PATH.
rem            PATH must use forward slashes (/).
rem
rem Toolchain paths (no need to edit this script; pick one):
rem   1) env vars:  SIM_MINGW / SIM_NINJA / SIM_CMAKE
rem   2) CLI flags: --mingw DIR / --ninja-dir DIR / --cmake-dir DIR
rem   3) auto:      gcc / ninja / cmake found on PATH
setlocal EnableExtensions

rem ---------- toolchain paths: defaults overridable by env ----------
if not defined SIM_MINGW set "SIM_MINGW=D:\msys64\mingw32\bin"
if not defined SIM_NINJA set "SIM_NINJA=D:\prebuilts\win64\ninja"
if not defined SIM_CMAKE set "SIM_CMAKE=D:\prebuilts\win64\cmake\bin"
set "MINGW=%SIM_MINGW%"
set "NINJA=%SIM_NINJA%"
set "CMAKE=%SIM_CMAKE%"
rem cmake -D paths must use forward slashes (backslash breaks parsing)
set "SRC_FWD=%CD:\=/%"

rem ---------- argument parsing ----------
set "BUILD_TYPE="
set "SDK_OPT="
set "EXTRA_ARGS="

:parse_args
if "%~1"=="" goto :parse_done
if /I "%~1"=="-h"         goto :help
if /I "%~1"=="--help"     goto :help
if /I "%~1"=="clean"      set BUILD_TYPE=clean& shift& goto :parse_args
if /I "%~1"=="debug"      set BUILD_TYPE=debug& shift& goto :parse_args
if /I "%~1"=="--sdk" (
    if "%~2"=="" (
        echo [ERROR] --sdk requires a path argument
        exit /b 1
    )
    set SDK_OPT=-DSDK_ROOT=%~2
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--mingw" (
    if "%~2"=="" (
        echo [ERROR] --mingw requires a path argument ^(mingw32\bin dir^)
        exit /b 1
    )
    set "MINGW=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--ninja-dir" (
    if "%~2"=="" (
        echo [ERROR] --ninja-dir requires a path argument
        exit /b 1
    )
    set "NINJA=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--cmake-dir" (
    if "%~2"=="" (
        echo [ERROR] --cmake-dir requires a path argument
        exit /b 1
    )
    set "CMAKE=%~2"
    shift
    shift
    goto :parse_args
)
echo [WARN] Unknown option: %~1
echo.
goto :help
:parse_done

if "%BUILD_TYPE%"=="clean" goto :clean
if "%BUILD_TYPE%"=="debug" goto :debug

rem ================= Release (default) =================
call :check_toolchain || exit /b 1

rem mingw32\bin must be on PATH so gcc finds cc1 and the mingw runtime DLLs
set "PATH=%MINGW%;%NINJA%;%CMAKE%;%PATH%"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=%MINGW_FWD%/gcc.exe %SDK_OPT% %EXTRA_ARGS%
if errorlevel 1 exit /b 1

cmake --build build
if errorlevel 1 exit /b 1

echo.
echo === BUILD OK: out\xysim.exe ===
if not "%SDK_OPT%"=="" echo     (SDK: %SDK_OPT%)
exit /b 0

rem ================= Debug (source-level gdb stepping; -g, no opt) =================
:debug
call :check_toolchain || exit /b 1
set "PATH=%MINGW%;%NINJA%;%CMAKE%;%PATH%"

cmake -S . -B build-dbg -G Ninja -DCMAKE_BUILD_TYPE=Debug -DXYSIM_OUTDIR=%SRC_FWD%/out-dbg -DCMAKE_C_COMPILER=%MINGW_FWD%/gcc.exe %SDK_OPT% %EXTRA_ARGS%
if errorlevel 1 exit /b 1

cmake --build build-dbg
if errorlevel 1 exit /b 1

echo.
echo === BUILD OK: out-dbg\xysim.exe (Debug, -g) ===
if not "%SDK_OPT%"=="" echo     (SDK: %SDK_OPT%)
echo     gdb: set PATH=%MINGW%;%%PATH%% then gdb out-dbg\xysim.exe
exit /b 0

rem ================= clean =================
:clean
echo Cleaning build directories...
for %%B in (build build-dbg) do (
    if exist %%B (
        rmdir /s /q %%B
    )
)
echo.
echo === Clean done ===
exit /b 0

rem ================= help =================
:help
echo.
echo XY4101 Simulator Build Script ^(Windows^)
echo.
echo Usage:
echo   build.bat                           Release build ^(out\xysim.exe^)
echo   build.bat debug                     Debug build ^(-g, no opt, out-dbg\xysim.exe^)
echo   build.bat clean                     Clean both configs
echo   build.bat --sdk D:/path/to/sdk      Release build with custom SDK path
echo   build.bat debug --sdk D:/path       Debug build with custom SDK path
echo   build.bat -h / --help               Show this help
echo.
echo Options:
echo   debug        Debug build with -g, no optimization. Output to out-dbg\.
echo   clean        Remove all build artifacts for both release and debug.
echo   --sdk PATH   Specify custom SDK root path (default: ..\LTEcat1_SDK_AP).
echo                Path must use forward slashes (/).
echo.
echo Toolchain paths (override, else auto-detected from PATH):
echo   --mingw DIR     mingw32\bin directory      (env SIM_MINGW, current: %MINGW%)
echo   --ninja-dir DIR directory containing ninja (env SIM_NINJA, current: %NINJA%)
echo   --cmake-dir DIR directory containing cmake (env SIM_CMAKE, current: %CMAKE%)
echo.
echo Examples:
echo   build.bat
echo   build.bat debug
echo   build.bat --sdk D:/my_custom_sdk
echo   build.bat --mingw C:/msys64/mingw32/bin --cmake-dir C:/tools/cmake/bin
echo   build.bat clean
echo.
exit /b 0

rem ================= toolchain detection =================
rem order: configured path (CLI/env/default) -> PATH auto-detect -> error.
rem label/goto layout on purpose: avoids delayed-expansion pitfalls
rem inside parenthesized blocks.
:check_toolchain
if exist "%MINGW%\gcc.exe" goto :mingw_ok
where gcc >nul 2>nul
if errorlevel 1 goto :no_gcc
for /f "delims=" %%G in ('where gcc') do set "MINGW=%%~dpG"
if "%MINGW:~-1%"=="\" set "MINGW=%MINGW:~0,-1%"
echo [INFO] gcc not at configured path, using PATH gcc: %MINGW%
:mingw_ok
if not exist "%MINGW%\gcc.exe" goto :no_gcc
set "MINGW_FWD=%MINGW:\=/%"

if exist "%NINJA%\ninja.exe" goto :ninja_ok
where ninja >nul 2>nul
if errorlevel 1 goto :no_ninja
for /f "delims=" %%N in ('where ninja') do set "NINJA=%%~dpN"
if "%NINJA:~-1%"=="\" set "NINJA=%NINJA:~0,-1%"
echo [INFO] ninja not at configured path, using PATH ninja: %NINJA%
:ninja_ok
if not exist "%NINJA%\ninja.exe" goto :no_ninja

if exist "%CMAKE%\cmake.exe" goto :cmake_ok
where cmake >nul 2>nul
if errorlevel 1 goto :no_cmake
for /f "delims=" %%C in ('where cmake') do set "CMAKE=%%~dpC"
if "%CMAKE:~-1%"=="\" set "CMAKE=%CMAKE:~0,-1%"
echo [INFO] cmake not at configured path, using PATH cmake: %CMAKE%
:cmake_ok
if not exist "%CMAKE%\cmake.exe" goto :no_cmake
exit /b 0

:no_gcc
echo [ERROR] gcc not found. Looked in: %MINGW%
echo         Install MSYS2 mingw-w64-i686-gcc, then either:
echo           - set SIM_MINGW=^<mingw32\bin dir^>, or
echo           - build.bat --mingw ^<dir^>, or
echo           - put mingw32\bin on PATH
exit /b 1

:no_ninja
echo [ERROR] ninja not found. Looked in: %NINJA%
echo         Use --ninja-dir / SIM_NINJA, or put ninja on PATH.
exit /b 1

:no_cmake
echo [ERROR] cmake not found. Looked in: %CMAKE%
echo         Use --cmake-dir / SIM_CMAKE, or put cmake on PATH.
exit /b 1
