@echo off
rem Installs Rawline as a Windows service that runs headless as LocalSystem. From an elevated prompt:
rem   install-service.bat ["C:\Program Files\Rawline\Rawline.exe"]
rem The service runs as SYSTEM, so Rawline.exe must sit in a folder only administrators can write:
rem install Rawline for all users (Program Files), not for one user under AppData.
setlocal

set "EXE=%~1"
if not defined EXE set "EXE=%ProgramFiles%\Rawline\Rawline.exe"
if not exist "%EXE%" (
    echo Rawline.exe not found: "%EXE%"
    exit /b 1
)
fltmc >nul 2>&1 || (
    echo Run this from an elevated Command Prompt.
    exit /b 1
)
sc.exe query Rawline >nul 2>&1 && (
    echo The Rawline service already exists; run uninstall-service.bat first.
    exit /b 1
)

rem No --data-dir: in service mode Rawline uses ProgramData\Rawline and creates it for SYSTEM and Administrators only.
sc.exe create Rawline binPath= "\"%EXE%\" --headless --service" start= delayed-auto obj= LocalSystem DisplayName= "Rawline (headless)" || exit /b 1
sc.exe description Rawline "Rawline proxy manager without a window; manage it with rawlinectl."
rem A crash or any non-zero exit (75: restart requested) restarts the service; the failure count resets after a day.
sc.exe failure Rawline reset= 86400 actions= restart/3000/restart/10000/restart/60000
sc.exe failureflag Rawline 1
sc.exe start Rawline
