@echo off
setlocal

if "%1"=="clean" goto clean

where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

if not defined SQLITE_INC if exist "%~dp0..\..\deps\sqlite3\include\sqlite3.h" set "SQLITE_INC=%~dp0..\..\deps\sqlite3\include"
if not defined SQLITE_LIB if exist "%~dp0..\..\deps\sqlite3\lib\sqlite3.lib" set "SQLITE_LIB=%~dp0..\..\deps\sqlite3\lib\sqlite3.lib"
if not defined SQLITE_LIB set SQLITE_LIB=sqlite3.lib
if exist "%~dp0..\..\deps\sqlite3\lib" set "PATH=%~dp0..\..\deps\sqlite3\lib;%PATH%"

if not exist bin mkdir bin
if exist "%~dp0..\..\deps\sqlite3\lib\*.dll" copy "%~dp0..\..\deps\sqlite3\lib\*.dll" bin\ >nul
if not exist "bin\clang_rt.asan_dynamic-x86_64.dll" for /f "delims=" %%F in ('where clang_rt.asan_dynamic-x86_64.dll 2^>nul') do copy "%%F" bin\ >nul
if not exist "bin\clang_rt.asan_dynamic-x86_64.dll" if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" bin\ >nul

cl /nologo /O2 /W4 /I"../../include" /I"%SQLITE_INC%" /std:c++14 /Fe:bin\test_oom.exe test_oom.cpp "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_oom.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

goto end

:clean
if exist bin rmdir /s /q bin

:end
endlocal
