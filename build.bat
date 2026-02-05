@echo off
REM Build script for tnfsd on Windows using Cygwin
REM Usage: build.bat [make arguments]
REM Example: build.bat clean
REM Example: build.bat LOCATE_DB=no

setlocal

REM Try to find Cygwin installation
if exist "C:\cygwin64\bin\bash.exe" (
    set CYGWIN_PATH=C:\cygwin64
) else if exist "C:\cygwin\bin\bash.exe" (
    set CYGWIN_PATH=C:\cygwin
) else if exist "C:\msys64\usr\bin\bash.exe" (
    set CYGWIN_PATH=C:\msys64\usr
) else if exist "D:\cygwin64\bin\bash.exe" (
    set CYGWIN_PATH=D:\cygwin64
) else if exist "D:\cygwin\bin\bash.exe" (
    set CYGWIN_PATH=D:\cygwin
) else if exist "D:\msys64\usr\bin\bash.exe" (
    set CYGWIN_PATH=D:\msys64\usr
) else (
    echo Error: Could not find Cygwin or MSYS2 installation.
    echo Please install Cygwin from https://cygwin.com/ or MSYS2 from https://www.msys2.org/
    echo Or set CYGWIN_PATH environment variable to your installation.
    exit /b 1
)

echo Using build environment: %CYGWIN_PATH%

REM Convert Windows path to Unix path for Cygwin
set "WIN_PATH=%~dp0"
set "WIN_PATH=%WIN_PATH:\=/%"
set "UNIX_PATH=/%WIN_PATH:~0,1%%WIN_PATH:~2%"

REM Run make through Cygwin bash
"%CYGWIN_PATH%\bin\bash.exe" -l -c "cd '%UNIX_PATH%' && make %*"
