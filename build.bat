@echo off
chcp 65001 >nul
cd /d "%~dp0"
set "PY="
py -3 --version >nul 2>nul && set "PY=py -3"
if not defined PY python --version >nul 2>nul && set "PY=python"
if not defined PY (
    echo ✖ Python 3 was not found. Ask the AI to install it - see SETUP.md.
    pause
    exit /b 1
)
echo Building the ROM...
echo.
%PY% tools\build.py
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
    echo ✔ Done. Click run.bat to play.
) else (
    echo ✖ The build failed. Copy the messages above and paste them to the AI.
)
pause
exit /b %RC%
