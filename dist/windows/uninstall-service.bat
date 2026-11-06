@echo off
rem Stops and removes the Rawline service. From an elevated prompt. The data in ProgramData\Rawline stays.
setlocal

fltmc >nul 2>&1 || (
    echo Run this from an elevated Command Prompt.
    exit /b 1
)
sc.exe query Rawline >nul 2>&1 || (
    echo The Rawline service is not installed.
    exit /b 0
)

sc.exe stop Rawline >nul 2>&1
rem A clean stop (core Stop, kill-switch teardown) can take up to 45 s.
set /a WAITED=0
:wait
sc.exe query Rawline | findstr /c:"STOPPED" >nul && goto delete
if %WAITED% geq 60 (
    echo The service did not stop within 60 s; it is removed once it stops.
    goto delete
)
set /a WAITED+=1
ping -n 2 127.0.0.1 >nul
goto wait

:delete
sc.exe delete Rawline
