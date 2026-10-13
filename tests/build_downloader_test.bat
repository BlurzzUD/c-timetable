@echo off
cd /d "%~dp0\.."
call tests\msvc_env.bat
if errorlevel 1 exit /b 1
cl /nologo /TC /std:c17 /O2 /W4 /WX /utf-8 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00 /Fobuild\ /Fe:tests\downloader_test.exe tests\downloader_test.c src\downloader.c src\timetable.c src\json_parser.c /link winhttp.lib
exit /b %errorlevel%

