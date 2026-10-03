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
%PY% tools\bundle.py
set "RC=%ERRORLEVEL%"
if "%RC%"=="0" (
    start "" "visualizer\visualizer.html"
    echo ✔ The visualizer is opening in your browser.
) else (
    echo ✖ Could not refresh the visualizer data. Copy the messages above and paste them to the AI.
)
pause
exit /b %RC%
