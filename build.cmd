@echo off
setlocal
set "installerVersion="
if /i "%~1"=="installer" (
    if "%~2"=="" (
        echo Usage: build.cmd installer ^<version^> 1>&2
        exit /b 1
    )
    set "installerVersion=%~2"
)
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

set "executable=%~dp0ScreenOff.exe"
set "buildMode=/DSCREEN_OFF_DEVELOPMENT"
if defined installerVersion (
    set "executable=%stem%.exe"
    set "buildMode="
)
rc /nologo /i "%~dp0." /fo "%stem%.res" "%~dp0ScreenOff.rc"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT %buildMode% /Fo"%stem%.obj" /Fe"%executable%" "%~dp0ScreenOff.cpp" "%stem%.res" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib imm32.lib
if errorlevel 1 exit /b 1
dumpbin /headers "%executable%" | findstr /C:"machine (x64)" /C:"subsystem (Windows GUI)"
if errorlevel 1 exit /b 1
if defined installerVersion goto installer
powershell.exe -NoProfile -Command "$ErrorActionPreference = 'Stop'; $folder = Split-Path -LiteralPath $env:executable; $shell = New-Object -ComObject WScript.Shell; $link = $shell.CreateShortcut((Join-Path $folder 'ScreenOff Settings.lnk')); $link.TargetPath = $env:executable; $link.Arguments = '--settings'; $link.WorkingDirectory = $folder; $link.Save()"
exit /b %errorlevel%

:installer
set "iscc=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%iscc%" set "iscc=ISCC.exe"
"%iscc%" "/DReleaseVersion=%installerVersion%" "/DAppExecutable=%executable%" "%~dp0ScreenOff.iss"
set "installerResult=%errorlevel%"
if exist "%executable%" del "%executable%"
exit /b %installerResult%

:test
cl /nologo /std:c++17 /EHsc /W4 /WX /MT /DSCREEN_OFF_TEST /Fo"%stem%.obj" /Fe"%stem%.exe" "%~dp0ScreenOff.cpp"
if errorlevel 1 exit /b 1
"%stem%.exe"
exit /b %errorlevel%
