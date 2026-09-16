@echo off
setlocal

where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

if not defined SQLITE_INC if exist "%~dp0..\..\deps\sqlite3\include\sqlite3.h" set "SQLITE_INC=%~dp0..\..\deps\sqlite3\include"
if not defined SQLITE_LIB if exist "%~dp0..\..\deps\sqlite3\lib\sqlite3.lib" set "SQLITE_LIB=%~dp0..\..\deps\sqlite3\lib\sqlite3.lib"
if exist "%~dp0..\..\deps\sqlite3\lib" set "PATH=%~dp0..\..\deps\sqlite3\lib;%PATH%"

if not defined SQLITE_LIB set SQLITE_LIB=sqlite3.lib

if not exist bin mkdir bin

cl /nologo /std:c++17 /EHsc /O2 /I"../../include" /I"%SQLITE_INC%" /Fe:bin\test_duo.exe test_duo.cpp %SQLITE_LIB%
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /I"../../include" /I"%SQLITE_INC%" /Fe:bin\test_duo_c.exe test_duo_c.c %SQLITE_LIB%
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_duo.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_duo_c.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

endlocal
