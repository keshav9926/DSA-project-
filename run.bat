@echo off
title ChessMind
set SCRIPT_DIR=%~dp0
set BRIDGE_DIR=%SCRIPT_DIR%bridge
set FRONTEND=%SCRIPT_DIR%frontend\index.html

if not exist "%BRIDGE_DIR%\chessmind.exe" (
    echo Engine not found. Run setup.bat first.
    pause & exit /b 1
)

:: Load env
for /f "usebackq tokens=1,2 delims==" %%a in ("%BRIDGE_DIR%\.env") do set %%a=%%b

echo Starting ChessMind bridge...
cd /d "%BRIDGE_DIR%"
start /B python -m uvicorn server:app --host 0.0.0.0 --port 8000 --log-level warning

timeout /t 3 /nobreak >nul

echo Opening game in browser...
start "" "%FRONTEND%"

echo.
echo Game is running! Close this window to stop.
echo.
pause
