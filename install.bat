@echo off
setlocal EnableDelayedExpansion

echo ================================================================
echo   Wsh Terminal Emulator — Installer
echo ================================================================
echo.

:: Check if Wsh.exe exists
if not exist "Wsh.exe" (
    echo ERROR: Wsh.exe not found in current directory.
    echo Please run this script from the directory containing Wsh.exe.
    echo Build first with: nmake
    pause
    exit /b 1
)

:: Set install destination
set "DEST=%LOCALAPPDATA%\Wsh"
echo Installing to: %DEST%
echo.

:: Create directory
if not exist "%DEST%" (
    mkdir "%DEST%"
    if errorlevel 1 (
        echo ERROR: Could not create %DEST%
        pause
        exit /b 1
    )
)

:: Copy executable
copy /Y "Wsh.exe" "%DEST%\Wsh.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy Wsh.exe to %DEST%
    pause
    exit /b 1
)
echo [OK] Copied Wsh.exe to %DEST%

:: Register in App Paths so it can be found by name
reg add "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\Wsh.exe" ^
    /ve /d "%DEST%\Wsh.exe" /f >nul
if errorlevel 1 (
    echo [WARN] Could not register App Paths entry.
) else (
    echo [OK] Registered App Paths entry.
)

:: Set Cascadia Code as console font
reg add "HKCU\Console" /v FaceName /t REG_SZ /d "Cascadia Code" /f >nul 2>&1
echo [OK] Set preferred console font.

:: Add "Open Wsh here" to Explorer background context menu
reg add "HKCU\SOFTWARE\Classes\Directory\Background\shell\Wsh" ^
    /ve /d "Open Wsh here" /f >nul
reg add "HKCU\SOFTWARE\Classes\Directory\Background\shell\Wsh" ^
    /v "Icon" /d "%DEST%\Wsh.exe,0" /f >nul
reg add "HKCU\SOFTWARE\Classes\Directory\Background\shell\Wsh\command" ^
    /ve /d "\"%DEST%\Wsh.exe\" --cwd \"%%V\"" /f >nul
echo [OK] Added Explorer context menu: "Open Wsh here" (background).

:: Add "Open Wsh here" to Explorer folder context menu
reg add "HKCU\SOFTWARE\Classes\Directory\shell\Wsh" ^
    /ve /d "Open Wsh here" /f >nul
reg add "HKCU\SOFTWARE\Classes\Directory\shell\Wsh" ^
    /v "Icon" /d "%DEST%\Wsh.exe,0" /f >nul
reg add "HKCU\SOFTWARE\Classes\Directory\shell\Wsh\command" ^
    /ve /d "\"%DEST%\Wsh.exe\" --cwd \"%%1\"" /f >nul
echo [OK] Added Explorer context menu: "Open Wsh here" (folder).

:: Optionally create a Desktop shortcut
set /p CREATE_SHORTCUT="Create Desktop shortcut? [Y/N]: "
if /i "%CREATE_SHORTCUT%"=="Y" (
    powershell -NoProfile -Command ^
        "$s=(New-Object -COM WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\Wsh.lnk');" ^
        "$s.TargetPath='%DEST%\Wsh.exe';" ^
        "$s.WorkingDirectory='%DEST%';" ^
        "$s.Description='Wsh Terminal Emulator';" ^
        "$s.Save()" >nul 2>&1
    echo [OK] Created Desktop shortcut.
)

:: Optionally add to PATH
set /p ADD_PATH="Add %DEST% to user PATH? [Y/N]: "
if /i "%ADD_PATH%"=="Y" (
    for /f "tokens=2*" %%A in ('reg query "HKCU\Environment" /v PATH 2^>nul') do set "CURRENT_PATH=%%B"
    if "!CURRENT_PATH!"=="" (
        reg add "HKCU\Environment" /v PATH /t REG_EXPAND_SZ /d "%DEST%" /f >nul
    ) else (
        reg add "HKCU\Environment" /v PATH /t REG_EXPAND_SZ /d "!CURRENT_PATH!;%DEST%" /f >nul
    )
    echo [OK] Added %DEST% to user PATH.
    echo NOTE: You may need to restart applications to pick up the new PATH.
)

echo.
echo ================================================================
echo   Installation complete!
echo.
echo   You can now:
echo     - Run Wsh from Start menu (search "Wsh")
echo     - Right-click any folder in Explorer to "Open Wsh here"
echo     - Type 'wsh' in any terminal (if added to PATH)
echo ================================================================
echo.
pause
