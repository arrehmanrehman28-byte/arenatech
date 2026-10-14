@echo off
rem ============================================================
rem  Arena Edit - one-click Windows release
rem  Made by arenatechofficial.pages.dev
rem
rem  Double-click, or from a prompt:
rem    release.bat                  core build (no media)
rem    release.bat C:\ffmpeg        full build: import/decode/export/STT
rem                                  (C:\ffmpeg = a FFmpeg dev prefix with
rem                                   include\, lib\, bin\ folders)
rem
rem  What it does: finds Visual Studio 2022, then runs tools\release.ps1,
rem  which builds, tests, and produces in dist\:
rem    - ArenaEdit-0.1.0-portable-win64.zip  (unzip, run, done)
rem    - ArenaEdit-0.1.0-Setup.exe           (installer)
rem
rem  Prereqs (one-time): VS2022 Desktop C++ + CMake + Ninja + Inno Setup 6
rem    choco install innosetup -y
rem ============================================================
setlocal
set "FFMPEG_DIR=%~1"
cd /d "%~dp0"

echo ============================================================
echo  Arena Edit release
echo ============================================================
echo.

rem --- 1. Find Visual Studio 2022 (vswhere ships with VS) ----------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio 2022 not found.
    echo         Install it first: https://visualstudio.microsoft.com/
    echo         Then add the "Desktop development with C++" workload.
    goto :fail
)
set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (
    echo [ERROR] VS2022 C++ workload not found.
    echo         In the Visual Studio Installer, add "Desktop development with C++".
    goto :fail
)
echo [1/4] Visual Studio: %VS_PATH%
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :fail
cd /d "%~dp0"

rem --- 2. Check the other tools -------------------------------------------
where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake not found. Install: winget install Kitware.CMake
    goto :fail
)
where ninja >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Ninja not found. Install: winget install Ninja-build.Ninja
    goto :fail
)
where iscc >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Inno Setup 6 not found. Install: choco install innosetup -y
    goto :fail
)
where powershell >nul 2>nul
if errorlevel 1 (
    echo [ERROR] PowerShell not found.
    goto :fail
)
echo [2/4] CMake + Ninja + Inno Setup: OK

rem --- 3. Run the release script ------------------------------------------
if defined FFMPEG_DIR (
    echo [3/4] Full build with media + STT; FFmpeg prefix: %FFMPEG_DIR%
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\release.ps1 -WithMedia -FfmpegDir "%FFMPEG_DIR%"
) else (
    echo [3/4] Core build (no media pipeline)
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\release.ps1
)
if errorlevel 1 goto :fail

rem --- 4. Show the artifacts ------------------------------------------------
echo [4/4] Done. Your files:
echo.
dir /b dist\ArenaEdit* 2>nul
echo.
echo  Portable:  dist\portable\            (unzip the .zip anywhere and double-click)
echo             "Run Arena Edit.bat"
echo.
pause
exit /b 0

:fail
echo.
echo ============================================================
echo  BUILD FAILED - read the messages above.
echo ============================================================
echo.
pause
exit /b 1
