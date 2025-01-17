@echo off

gcc winshell.c -Wall -Wextra -O2 -o winshell.exe

if %errorlevel% equ 0 (
    echo Build successful, running winshell.exe
    winshell.exe
) else ( 
    echo Build script failed.
)
