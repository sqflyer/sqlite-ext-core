@echo off
setlocal

if "%1"=="clean" goto clean

where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

if not defined SQLITE_INC if exist "%~dp0..\deps\sqlite3\include\sqlite3.h" set "SQLITE_INC=%~dp0..\deps\sqlite3\include"
if not defined SQLITE_LIB if exist "%~dp0..\deps\sqlite3\lib\sqlite3.lib" set "SQLITE_LIB=%~dp0..\deps\sqlite3\lib\sqlite3.lib"
if not defined SQLITE_LIB set SQLITE_LIB=sqlite3.lib
if exist "%~dp0..\deps\sqlite3\lib" set "PATH=%~dp0..\deps\sqlite3\lib;%PATH%"
if exist "%~dp0..\deps\sqlite3\bin" set "PATH=%~dp0..\deps\sqlite3\bin;%PATH%"

set "SQLITE_CMD=sqlite3"
if exist "%~dp0..\deps\sqlite3\bin\sqlite3.exe" set "SQLITE_CMD=%~dp0..\deps\sqlite3\bin\sqlite3.exe"

if not exist build mkdir build

cl /nologo /O2 /W4 /I"../include" /I"%SQLITE_INC%" /std:c++14 /GR- /EHs-c- /LD /Fe:build\libcoro_cpp_example.dll example.cpp /link /NODEFAULTLIB:msvcprt.lib /NODEFAULTLIB:libcpmt.lib
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

"%SQLITE_CMD%" :memory: ".read example.sql"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

goto end

:clean
if exist build rmdir /s /q build

:end
endlocal
