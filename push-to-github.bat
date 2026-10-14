@echo off
rem ============================================================
rem  Arena Edit - push to GitHub (one time)
rem  Made by arenatechofficial.pages.dev
rem
rem  Before running:
rem    1. Create a NEW EMPTY repository on github.com
rem       (https://github.com/new) - do NOT check "Add a README".
rem    2. Create a personal access token with "repo" scope:
rem       https://github.com/settings/tokens (classic token is fine).
rem
rem  Double-click this file and answer the two questions.
rem  After the push, GitHub builds the full Windows app for you:
rem    github.com/yourname/yourrepo  ->  "Actions" tab
rem    -> "release-windows-full" -> "Run workflow"
rem    -> when green: "Artifacts" -> download the zip (the app!).
rem ============================================================
setlocal
cd /d "%~dp0"

echo ============================================================
echo  Push Arena Edit to GitHub
echo ============================================================
echo.
where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Git not found. Install it first: https://git-scm.com/
    goto :end
)
set "REPO="
set /p "REPO=GitHub repo  (owner/name, e.g. myuser/arena-edit): "
if "%REPO%"=="" goto :end
set "TOKEN="
set /p "TOKEN=Personal access token (shown on screen; used once - revoke after): "
if "%TOKEN%"=="" goto :end

echo.
echo Pushing all commits to https://github.com/%REPO% ...
git remote remove origin 2>nul
git remote add origin "https://x-access-token:%TOKEN%@github.com/%REPO%.git"
git push -u origin main
set "RC=%ERRORLEVEL%"

rem Remove the token from the saved remote URL immediately.
git remote set-url origin "https://github.com/%REPO%.git"

if not "%RC%"=="0" (
    echo.
    echo [ERROR] Push failed. Common causes:
    echo   - repo not created yet, or not empty
    echo   - token missing "repo" scope
    echo   - wrong owner/name
    goto :end
)

echo.
echo ============================================================
echo  Pushed. Now GitHub builds your Windows app:
echo
echo   1. Open  https://github.com/%REPO%
echo   2. Click "Actions"  (second tab)
echo   3. "release-windows-full"  ->  "Run workflow"
echo   4. Wait ~25 minutes (FFmpeg build), then on the green
echo      run click "Artifacts" and download the zip:
echo        ArenaEdit-*-portable-win64.zip   (the app - unzip and run)
echo        ArenaEdit-*-Setup.exe            (installer)
echo
echo  The token was used once and removed from this repo's config.
echo  You may revoke it at https://github.com/settings/tokens
echo ============================================================
echo.
pause
exit /b 0

:end
echo.
pause
exit /b 1
