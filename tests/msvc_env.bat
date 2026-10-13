@echo off
rem Share the existing installed MSVC environment with the calling test script.
where cl.exe >nul 2>nul
if not errorlevel 1 exit /b 0
set "CTIMETABLE_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%CTIMETABLE_TEST_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%I in (`"%CTIMETABLE_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CTIMETABLE_TEST_VS=%%I"
if not defined CTIMETABLE_TEST_VS exit /b 1
call "%CTIMETABLE_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
