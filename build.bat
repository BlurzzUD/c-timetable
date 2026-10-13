@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Build the pure-C native Windows application with an installed C compiler.
where cl.exe >nul 2>nul
if not errorlevel 1 goto :msvc

set "CTIMETABLE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%CTIMETABLE_VSWHERE%" goto :try_gcc
for /f "usebackq delims=" %%I in (`"%CTIMETABLE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CTIMETABLE_VS=%%I"
if not defined CTIMETABLE_VS goto :try_gcc
if not exist "%CTIMETABLE_VS%\VC\Auxiliary\Build\vcvars64.bat" goto :try_gcc
call "%CTIMETABLE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

:msvc
if not exist build mkdir build
cl /nologo /TC /std:c17 /O2 /W4 /utf-8 /MT ^
  /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00 ^
  /Fobuild\ /Fe:ctimetable.exe src\*.c ^
  /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED ^
  /MANIFESTINPUT:src\app.manifest ^
  winhttp.lib comctl32.lib gdi32.lib user32.lib shell32.lib advapi32.lib
if errorlevel 1 exit /b 1
echo Built %CD%\ctimetable.exe
exit /b 0

:try_gcc
where gcc.exe >nul 2>nul
if errorlevel 1 goto :no_compiler
if not exist build mkdir build
where windres.exe >nul 2>nul
if errorlevel 1 goto :no_windres
windres -I src src\app.rc -O coff -o build\app-res.o
if errorlevel 1 exit /b 1
gcc -std=c17 -O2 -Wall -Wextra -municode -mwindows ^
  -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0A00 ^
  -finput-charset=UTF-8 -fexec-charset=UTF-8 src\*.c build\app-res.o ^
  -o ctimetable.exe ^
  -lwinhttp -lcomctl32 -lgdi32 -luser32 -lshell32 -ladvapi32
if errorlevel 1 exit /b 1
echo Built %CD%\ctimetable.exe
exit /b 0

:no_windres
echo ERROR: MinGW GCC was found, but windres.exe is missing from PATH.
exit /b 1

:no_compiler
echo ERROR: No supported Windows C compiler found.
echo Install Visual Studio Build Tools with the C++ build tools and Windows SDK,
echo or put a MinGW-w64 GCC toolchain with windres on PATH, then rerun build.bat.
exit /b 1
