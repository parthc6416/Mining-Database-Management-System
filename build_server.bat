@echo off
setlocal
echo Building Mining Database Management System - Web Server...
gcc -Wall -Wextra -O2 server.c -o miningweb.exe -lodbc32 -lws2_32 -ladvapi32
if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    echo Check GCC, ODBC headers (sql.h and sqlext.h), and 32/64-bit
    echo compatibility between GCC and your Oracle ODBC driver.
    pause
    exit /b 1
)
echo.
echo Build successful.
echo Run miningweb.exe, then open http://127.0.0.1:8090 in a browser.
pause
