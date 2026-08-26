@echo off
setlocal

if "%1"=="clean" goto clean

where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

if not defined SQLITE_INC if exist "%~dp0..\..\deps\sqlite3\include\sqlite3.h" set "SQLITE_INC=%~dp0..\..\deps\sqlite3\include"
if not defined SQLITE_LIB if exist "%~dp0..\..\deps\sqlite3\lib\sqlite3.lib" set "SQLITE_LIB=%~dp0..\..\deps\sqlite3\lib\sqlite3.lib"
if not defined SQLITE_LIB set SQLITE_LIB=sqlite3.lib
if exist "%~dp0..\..\deps\sqlite3\lib" set "PATH=%~dp0..\..\deps\sqlite3\lib;%PATH%"

if "%1"=="test-c" goto test_c
if "%1"=="test-cpp" goto test_cpp

:all
call :test_c
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
goto end

:test_c
if not exist bin mkdir bin
if exist "%~dp0..\..\deps\sqlite3\lib\*.dll" copy "%~dp0..\..\deps\sqlite3\lib\*.dll" bin\ >nul
cl /nologo /O2 /W4 /Zi /fsanitize=address /MD /I"../../include" /I"%SQLITE_INC%" /std:c11 /Fe:bin\test_smart_ptr_c.exe test_smart_ptr.c "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
bin\test_smart_ptr_c.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
goto :eof

:test_cpp
if not exist bin mkdir bin
if exist "%~dp0..\..\deps\sqlite3\lib\*.dll" copy "%~dp0..\..\deps\sqlite3\lib\*.dll" bin\ >nul
cl /nologo /O2 /W4 /Zi /fsanitize=address /MD /I"../../include" /I"%SQLITE_INC%" /std:c++14 /Fe:bin\test_smart_ptr_cpp.exe test_smart_ptr.cpp "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
bin\test_smart_ptr_cpp.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
goto :eof

:clean
if exist bin rmdir /s /q bin

:end
endlocal
