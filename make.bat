@echo off
setlocal

where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

if not defined SQLITE_INC if exist "%~dp0deps\sqlite3\include\sqlite3.h" set "SQLITE_INC=%~dp0deps\sqlite3\include"
if not defined SQLITE_LIB if exist "%~dp0deps\sqlite3\lib\sqlite3.lib" set "SQLITE_LIB=%~dp0deps\sqlite3\lib\sqlite3.lib"
if exist "%~dp0deps\sqlite3\lib" set "PATH=%~dp0deps\sqlite3\lib;%PATH%"

if not defined SQLITE_LIB set SQLITE_LIB=sqlite3.lib

set TARGET=%1
if "%TARGET%"=="" set TARGET=test

if "%TARGET%"=="clean" goto clean
if "%TARGET%"=="test" goto test
if "%TARGET%"=="test-asan" ( call :test_asan & goto end )
if "%TARGET%"=="test-time" ( call :test_time & goto end )
if "%TARGET%"=="test-oom" ( call :test_oom & goto end )
if "%TARGET%"=="test-multi-tu" ( call :test_multi_tu & goto end )
if "%TARGET%"=="test-ext-state" ( call :test_ext_state & goto end )
if "%TARGET%"=="test-cpp-value" ( call :test_cpp_value & goto end )
if "%TARGET%"=="test-locks" ( call :test_locks & goto end )
if "%TARGET%"=="test-cpp-allocator" ( call :test_cpp_allocator & goto end )
if "%TARGET%"=="test-cpp-smart-ptr" ( call :test_cpp_smart_ptr & goto end )
if "%TARGET%"=="test-cpp-udf" ( call :test_cpp_udf & goto end )
if "%TARGET%"=="test-cpp-aggregate" ( call :test_cpp_aggregate & goto end )
if "%TARGET%"=="test-cpp-statement" ( call :test_cpp_statement & goto end )
if "%TARGET%"=="test-cpp-tvf" ( call :test_cpp_tvf & goto end )
if "%TARGET%"=="test-cpp-transaction" ( call :test_cpp_transaction & goto end )
if "%TARGET%"=="test-cpp-db" ( call :test_cpp_db & goto end )
if "%TARGET%"=="test-cpp-buffer" ( call :test_cpp_buffer & goto end )
if "%TARGET%"=="test-cpp-blob-stream" ( call :test_cpp_blob_stream & goto end )
if "%TARGET%"=="test-cpp-backup" ( call :test_cpp_backup & goto end )
if "%TARGET%"=="test-cpp-vtab" ( call :test_cpp_vtab & goto end )
if "%TARGET%"=="test-cpp-extension" ( call :test_cpp_extension & goto end )
if "%TARGET%"=="test-threads" ( call :test_threads & goto end )
if "%TARGET%"=="example" ( call :example & goto end )
if "%TARGET%"=="example-c" ( call :example_c & goto end )

echo Unknown target: %TARGET%
exit /b 1

:test
call :test_ext_state
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_value
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_locks
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_time
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_oom
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_multi_tu
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_allocator
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_smart_ptr
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_udf
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_aggregate
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_statement
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_tvf
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_transaction
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_db
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_buffer
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_blob_stream
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_backup
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_vtab
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
call :test_cpp_extension
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo.
echo All MSVC tests passed successfully!
goto end

:test_asan
echo [Running MSVC AddressSanitizer (ASan)]
where cl >nul 2>nul
if %ERRORLEVEL% neq 0 if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if not defined SQLITE_INC if exist "%~dp0deps\sqlite3\include\sqlite3.h" set "SQLITE_INC=%~dp0deps\sqlite3\include"
if not defined SQLITE_LIB if exist "%~dp0deps\sqlite3\lib\sqlite3.lib" set "SQLITE_LIB=%~dp0deps\sqlite3\lib\sqlite3.lib"
if not defined SQLITE_LIB set SQLITE_LIB=sqlite3.lib
if exist "%~dp0deps\sqlite3\lib" set "PATH=%~dp0deps\sqlite3\lib;%PATH%"
if not exist tests\oom_safety\bin mkdir tests\oom_safety\bin
if exist "%~dp0deps\sqlite3\lib\*.dll" copy "%~dp0deps\sqlite3\lib\*.dll" tests\oom_safety\bin\ >nul
cl /nologo /O2 /Zi /fsanitize=address /MD /I"include" /I"%SQLITE_INC%" /std:c++14 /Fe:tests\oom_safety\bin\test_msvc_asan.exe tests\oom_safety\test_oom.cpp "%SQLITE_LIB%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
tests\oom_safety\bin\test_msvc_asan.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
goto :eof

:test_time
echo [Running test-time]
cd tests\time && call make.bat && cd ..\..
goto :eof

:test_oom
echo [Running test-oom]
cd tests\oom_safety && call make.bat && cd ..\..
goto :eof

:test_multi_tu
echo [Running test-multi-tu]
cd tests\multi_tu && call make.bat && cd ..\..
goto :eof

:test_ext_state
echo [Running test-ext-state]
cd tests\ext_state && call make.bat && cd ..\..
goto :eof

:test_cpp_value
echo [Running test-cpp-value]
cd tests\cpp_value && call make.bat && cd ..\..
goto :eof

:test_locks
echo [Running test-locks]
cd tests\locks && call make.bat && cd ..\..
goto :eof

:test_cpp_allocator
echo [Running test-cpp-allocator]
cd tests\allocator && call make.bat && cd ..\..
goto :eof

:test_cpp_smart_ptr
echo [Running test-cpp-smart-ptr]
cd tests\smart_ptr && call make.bat && cd ..\..
goto :eof

:test_cpp_udf
echo [Running test-cpp-udf]
cd tests\cpp_udf && call make.bat && cd ..\..
goto :eof

:test_cpp_aggregate
echo [Running test-cpp-aggregate]
cd tests\cpp_aggregate && call make.bat && cd ..\..
goto :eof

:test_cpp_statement
echo [Running test-cpp-statement]
cd tests\cpp_statement && call make.bat && cd ..\..
goto :eof

:test_cpp_tvf
echo [Running test-cpp-tvf]
cd tests\cpp_tvf && call make.bat && cd ..\..
goto :eof

:test_cpp_transaction
echo [Running test-cpp-transaction]
cd tests\cpp_transaction && call make.bat && cd ..\..
goto :eof

:test_cpp_db
echo [Running test-cpp-db]
cd tests\cpp_db && call make.bat && cd ..\..
goto :eof

:test_cpp_buffer
echo [Running test-cpp-buffer]
cd tests\cpp_buffer && call make.bat && cd ..\..
goto :eof

:test_cpp_blob_stream
echo [Running test-cpp-blob-stream]
cd tests\cpp_blob_stream && call make.bat && cd ..\..
goto :eof

:test_cpp_backup
echo [Running test-cpp-backup]
cd tests\cpp_backup && call make.bat && cd ..\..
goto :eof

:test_cpp_vtab
echo [Running test-cpp-vtab]
cd tests\cpp_vtab && call make.bat && cd ..\..
goto :eof

:test_cpp_extension
echo [Running test-cpp-extension]
cd tests\cpp_extension && call make.bat && cd ..\..
goto :eof

:test_threads
echo [Running test-threads]
cd tests\threads && call make.bat && cd ..\..
goto :eof

:example
echo [Running C++ Example]
cd examples && call make.bat && cd ..
goto :eof

:example_c
echo [Running Pure C Example]
cd example-c && call make.bat && cd ..
goto :eof

:clean
echo [Cleaning build artifacts]
cd tests\ext_state && call make.bat clean && cd ..\..
cd tests\cpp_value && call make.bat clean && cd ..\..
cd tests\locks && call make.bat clean && cd ..\..
cd tests\time && call make.bat clean && cd ..\..
cd tests\allocator && call make.bat clean && cd ..\..
cd tests\smart_ptr && call make.bat clean && cd ..\..
cd tests\cpp_udf && call make.bat clean && cd ..\..
cd tests\cpp_aggregate && call make.bat clean && cd ..\..
cd tests\cpp_statement && call make.bat clean && cd ..\..
cd tests\cpp_tvf && call make.bat clean && cd ..\..
cd tests\cpp_transaction && call make.bat clean && cd ..\..
cd tests\cpp_db && call make.bat clean && cd ..\..
cd tests\cpp_buffer && call make.bat clean && cd ..\..
cd tests\cpp_blob_stream && call make.bat clean && cd ..\..
cd tests\cpp_backup && call make.bat clean && cd ..\..
cd tests\cpp_vtab && call make.bat clean && cd ..\..
cd tests\cpp_extension && call make.bat clean && cd ..\..
cd tests\threads && call make.bat clean && cd ..\..
cd examples && call make.bat clean && cd ..
cd example-c && call make.bat clean && cd ..
echo Clean completed.
goto end

:end
endlocal
