@echo off
setlocal

if exist "C:\msys64\mingw64\bin" set "PATH=C:\msys64\mingw64\bin;%PATH%"
if exist "C:\msys64\ucrt64\bin" set "PATH=C:\msys64\ucrt64\bin;%PATH%"
if exist "C:\msys64\usr\bin" set "PATH=C:\msys64\usr\bin;%PATH%"

if "%1"=="clean" goto clean
if "%1"=="test-c" goto test_c
if "%1"=="test-cpp" goto test_cpp

:all
call :test_c
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
goto end

:test_c
call :build_c
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo Running Go concurrency and isolation stress test with C extension...
set EXT_PATH=%~dp0bin\c_ext\libmyext.dll
cd go_loader
..\bin\concurrency_test.exe
if %ERRORLEVEL% neq 0 ( cd .. & exit /b %ERRORLEVEL% )
echo Running Go dynamic/lazy loading test with C extension...
..\bin\lazy_test.exe
if %ERRORLEVEL% neq 0 ( cd .. & exit /b %ERRORLEVEL% )
cd ..
goto :eof

:test_cpp
call :build_cpp
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo Running Go concurrency and isolation stress test with C++ extension...
set EXT_PATH=%~dp0bin\cpp_ext\libmyext.dll
cd go_loader
..\bin\concurrency_test.exe
if %ERRORLEVEL% neq 0 ( cd .. & exit /b %ERRORLEVEL% )
echo Running Go dynamic/lazy loading test with C++ extension...
..\bin\lazy_test.exe
if %ERRORLEVEL% neq 0 ( cd .. & exit /b %ERRORLEVEL% )
cd ..
goto :eof

:build_go
if not exist bin mkdir bin
if exist bin\concurrency_test.exe if exist bin\lazy_test.exe goto :eof
if exist "C:\msys64\mingw64\bin" set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
if exist "C:\msys64\mingw64\bin\gcc.exe" set "CC=C:\msys64\mingw64\bin\gcc.exe"
echo Building Go integration tests...
cd go_loader
go build -tags sqlite_extension -o ..\bin\concurrency_test.exe concurrency.go
if %ERRORLEVEL% neq 0 ( cd .. & exit /b %ERRORLEVEL% )
go build -tags sqlite_extension -o ..\bin\lazy_test.exe lazy_load.go
if %ERRORLEVEL% neq 0 ( cd .. & exit /b %ERRORLEVEL% )
cd ..
goto :eof

:build_c
call :build_go
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
if not exist bin\c_ext mkdir bin\c_ext
echo Building C extension...
where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if not defined SQLITE_INC if exist "%~dp0..\..\deps\sqlite3\include\sqlite3.h" set "SQLITE_INC=%~dp0..\..\deps\sqlite3\include"
cl /nologo /O2 /W4 /I"../../include" /I"%SQLITE_INC%" /LD /Fe:bin\c_ext\libmyext.dll c_extension\myext.c
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
goto :eof

:build_cpp
call :build_go
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
if not exist bin\cpp_ext mkdir bin\cpp_ext
echo Building C++ extension...
where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if not defined SQLITE_INC if exist "%~dp0..\..\deps\sqlite3\include\sqlite3.h" set "SQLITE_INC=%~dp0..\..\deps\sqlite3\include"
cl /nologo /O2 /W4 /I"../../include" /I"%SQLITE_INC%" /std:c++14 /GR- /EHs-c- /LD /Fe:bin\cpp_ext\libmyext.dll cpp_extension\myext.cpp /link /NODEFAULTLIB:msvcprt.lib /NODEFAULTLIB:libcpmt.lib
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
goto :eof

:clean
if exist bin rmdir /s /q bin

:end
endlocal
