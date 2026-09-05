@echo off
cd /d "%~dp0"
if not exist build\MingEchoServer.exe (
    echo MingEchoServer.exe not found. Building first...
    call build_gcc.bat
    if errorlevel 1 exit /b 1
)
echo Starting MingEchoServer (pure C)...
build\MingEchoServer.exe
