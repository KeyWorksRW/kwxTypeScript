@echo off
setlocal

rem Type-check the kwxTypeScript probe lane. probe.wxui generates
rem probe_mainframe.ts and probe_dlg.ts into tests\quick_probe\gen\; probe_main.ts
rem is hand-written. Probe output never goes to tests\quick.
rem
rem   build_probe.cmd        check tests\quick_probe (the probe gate)
rem   build_probe.cmd help   this text

cd /D "%~dp0"

if /I "%~1"=="help" goto :usage
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="/?" goto :usage

where deno >nul 2>&1
if errorlevel 1 (
    echo ERROR: deno not found on PATH.
    exit /b 1
)

if not exist "tests\quick_probe\probe_main.ts" goto :no_main

deno check --no-lock tests\quick_probe\probe_main.ts
if errorlevel 1 (
    echo ERROR: deno check failed.
    exit /b 1
)

exit /b 0

:usage
echo Usage: build_probe.cmd
echo.
echo   build_probe.cmd        deno check tests\quick_probe and exit with its status
echo   build_probe.cmd help   this text
exit /b 0

:no_main
echo ERROR: tests\quick_probe\probe_main.ts does not exist.
exit /b 1
