@echo off
setlocal

if "%1"=="clean" goto clean

where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

if not exist bin mkdir bin

cl /nologo /O2 /W4 /Zi /fsanitize=address /MD /I"../../include" /std:c11 /Fe:bin\test_time_c.exe test_time_c.c
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi /fsanitize=address /MD /I"../../include" /std:c++14 /Fe:bin\test_time.exe test_time.cpp
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_time_c.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_time.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

goto end

:clean
if exist bin rmdir /s /q bin
del /f /q *.obj *.o *.pdb *.ilk *.exp *.lib 2>nul

:end
endlocal
