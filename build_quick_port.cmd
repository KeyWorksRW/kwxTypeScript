@echo off
setlocal

rem Type-check the kwxTypeScript QuickPort app. Deno is the TypeScript compiler
rem here; nothing is executed and no window is created. Derived from
rem build_quick.cmd -- same compiler, same import map, only the folder differs.
rem
rem   build_quick_port.cmd        check tests\quick_port (the port lane)
rem   build_quick_port.cmd help   this text

cd /D "%~dp0"

if /I "%~1"=="help" goto :usage
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="/?" goto :usage

where deno >nul 2>&1
if errorlevel 1 (
    echo ERROR: deno not found on PATH.
    exit /b 1
)

if not exist "tests\quick_port\quick_port_main.ts" goto :no_main

deno check --no-lock tests\quick_port\quick_port_main.ts
if errorlevel 1 (
    echo ERROR: deno check failed.
    exit /b 1
)

exit /b 0

:usage
echo Usage: build_quick_port.cmd
echo.
echo   build_quick_port.cmd        deno check tests\quick_port and exit with its status
echo   build_quick_port.cmd help   this text
exit /b 0

:no_main
echo ERROR: tests\quick_port\quick_port_main.ts does not exist.
exit /b 1
