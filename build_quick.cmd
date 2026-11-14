@echo off
setlocal

rem Type-check the kwxTypeScript quick test app. Deno is the TypeScript
rem compiler here; nothing is executed and no window is created.
rem
rem   build_quick.cmd        check tests\quick (the verification gate)
rem   build_quick.cmd help   this text

cd /D "%~dp0"

if /I "%~1"=="help" goto :usage
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="/?" goto :usage

where deno >nul 2>&1
if errorlevel 1 (
    echo ERROR: deno not found on PATH.
    exit /b 1
)

if not exist "tests\quick\quick_main.ts" goto :no_main

deno check --no-lock tests\quick\quick_main.ts
if errorlevel 1 (
    echo ERROR: deno check failed.
    exit /b 1
)

exit /b 0

:usage
echo Usage: build_quick.cmd
echo.
echo   build_quick.cmd        deno check tests\quick and exit with its status
echo   build_quick.cmd help   this text
exit /b 0

:no_main
echo ERROR: tests\quick\quick_main.ts does not exist.
exit /b 1
