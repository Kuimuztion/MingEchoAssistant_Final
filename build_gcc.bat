@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

if not exist build mkdir build

where gcc >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Cannot find gcc. Please install MinGW-w64 and add gcc.exe to PATH.
    exit /b 1
)

echo [1/3] Building mec_import.exe (resource importer)...
gcc -std=c11 -O2 -Wall -Wextra ^
    native\server\import_main.c ^
    -o build\mec_import.exe ^
    -lkernel32 -luser32
if errorlevel 1 (
    echo [ERROR] importer build failed.
    exit /b 1
)

echo [2/3] Importing Project resources and generating database...
build\mec_import.exe --root .
if errorlevel 1 exit /b 1

echo [3/3] Building MingEchoServer.exe (pure C, no Python)...
set SRC=native\server\main.c native\server\http_server.c native\server\routes.c native\server\mec_config.c native\server\mec_kuro.c native\server\mec_cloud.c native\server\mec_gacha.c ^
    native\mec_core.c native\core\score.c native\core\recommend.c native\core\panel.c native\core\echo_grade.c ^
    native\parser\stat_parser.c native\parser\chinese_dict.c ^
    native\database\loader.c native\database\cache.c native\database\generator.c

gcc -std=c11 -O2 -Wall -Wextra -DMEC_STATIC ^
    -Inative\api -Inative\common ^
    %SRC% ^
    -o build\MingEchoServer.exe ^
    -lws2_32 -lwinhttp -lshell32 -lkernel32 -luser32 -lgdi32 -lm

if errorlevel 1 (
    echo [ERROR] server build failed.
    exit /b 1
)

echo.
echo [OK] build\MingEchoServer.exe created (pure C, no Python dependency).
echo Run: run.bat
endlocal
