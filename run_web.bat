@echo off
setlocal
cd /d "%~dp0"

set PORT=8090
if not "%1"=="" set PORT=%1

if not exist miningweb.exe goto build
rem Rebuild when the source is newer than the executable.
for /f %%i in ('dir /b /o-d server.c miningweb.exe 2^>nul') do (
    if /i "%%i"=="server.c" goto build
    goto run
)

:build
echo Building the web server...
gcc -Wall -Wextra -O2 server.c -o miningweb.exe -lodbc32 -lws2_32 -ladvapi32
if errorlevel 1 (
    echo.
    echo BUILD FAILED. See build_server.bat notes and README.txt.
    pause
    exit /b 1
)
echo Build successful.
echo.

:run
start "" http://127.0.0.1:%PORT%
miningweb.exe %PORT%
