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

if not defined SAN_FLAGS set SAN_FLAGS=

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c11 /Fe:bin\test_thread_c.exe test_thread_c.c "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c11 /Fe:bin\test_cond_c.exe test_cond_c.c "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c11 /Fe:bin\test_coro_c.exe test_coro_c.c "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c11 /Fe:bin\test_coro_sched_c.exe test_coro_sched_c.c "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c11 /Fe:bin\test_coro_ext_pool_c.exe test_coro_ext_pool_c.c "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c++14 /Fe:bin\test_thread.exe test_thread.cpp "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c++14 /Fe:bin\test_cond.exe test_cond.cpp "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c++14 /Fe:bin\test_coro.exe test_coro.cpp "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c++14 /Fe:bin\test_coro_sched.exe test_coro_sched.cpp "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /nologo /O2 /W4 /Zi %SAN_FLAGS% /MD /I"../../include" /I"../../include/async" /I"%SQLITE_INC%" /std:c++14 /Fe:bin\test_coro_ext_pool.exe test_coro_ext_pool.cpp "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_thread_c.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_cond_c.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_coro_c.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_coro_sched_c.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_coro_ext_pool_c.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_thread.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_cond.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_coro.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_coro_sched.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

bin\test_coro_ext_pool.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

goto end

:clean
if exist bin rmdir /s /q bin
del /f /q *.obj *.o *.pdb *.ilk *.exp *.lib 2>nul

:end
endlocal
