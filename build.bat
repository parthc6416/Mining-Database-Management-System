@echo off
setlocal
echo Building Mining Database Management System...
gcc -Wall -Wextra main.c -o miningdbms.exe -lodbc32
if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    echo Check GCC, ODBC headers (sql.h and sqlext.h), and architecture compatibility.
    pause
    exit /b 1
)
echo.
echo Build successful. Run miningdbms.exe to start the application.
pause
