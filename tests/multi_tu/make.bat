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

cl /nologo /O2 /W4 /Zi /fsanitize=address /MD /I"../../include" /I"%SQLITE_INC%" /std:c++14 /c /Fo:bin\test_tu_a.obj test_tu_a.cpp
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi /fsanitize=address /MD /I"../../include" /I"%SQLITE_INC%" /std:c++14 /c /Fo:bin\test_tu_b.obj test_tu_b.cpp
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi /fsanitize=address /MD /I"../../include" /I"%SQLITE_INC%" /std:c++14 /c /Fo:bin\test_tu_main.obj test_tu_main.cpp
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /fsanitize=address /MD /Fe:bin\test_multi_tu.exe bin\test_tu_a.obj bin\test_tu_b.obj bin\test_tu_main.obj "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_multi_tu.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

goto end

:clean
if exist bin rmdir /s /q bin
del /f /q *.obj *.o *.pdb *.ilk *.exp *.lib 2>nul

:end
endlocal
