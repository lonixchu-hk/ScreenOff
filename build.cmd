@echo off
setlocal
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "delims=" %%I in ('call "%vswhere%" -all -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "vsdir=%%I"
if not defined vsdir (
    echo MSVC x64 build tools were not found. 1>&2
    exit /b 1
)
call "%vsdir%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

set "stem=%TEMP%\ScreenOff-%RANDOM%-%RANDOM%"
if /i "%~1"=="test" goto test

rc /nologo /i "%~dp0." /fo "%stem%.res" "%~dp0ScreenOff.rc"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Fo"%stem%.obj" /Fe"%~dp0ScreenOff.exe" "%~dp0ScreenOff.cpp" "%stem%.res" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib imm32.lib
if errorlevel 1 exit /b 1
dumpbin /headers "%~dp0ScreenOff.exe" | findstr /C:"machine (x64)" /C:"subsystem (Windows GUI)"
exit /b %errorlevel%

:test
cl /nologo /std:c++17 /EHsc /W4 /WX /MT /DSCREEN_OFF_TEST /Fo"%stem%.obj" /Fe"%stem%.exe" "%~dp0ScreenOff.cpp"
if errorlevel 1 exit /b 1
"%stem%.exe"
exit /b %errorlevel%
