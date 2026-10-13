@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tests\msvc_env.bat
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /TC /std:c17 /W4 /WX /O2 /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00 /FI"%CD%\tests\gui_compile_stub.h" /Fobuild\main-check.obj /c src\main.c
exit /b %errorlevel%
