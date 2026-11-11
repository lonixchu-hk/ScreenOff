# ScreenOff

ScreenOff is a small Windows utility that blacks out every active monitor while your apps keep running. It draws pure black (`#000000`), hides the pointer, and asks Windows to keep the displays awake during the blackout.

## Use ScreenOff

Run [ScreenOff.exe](ScreenOff.exe) on Windows 11 x64 to start the blackout immediately. To return to your desktop, **double-click the left mouse button** or **tap Space twice quickly**.

Both gestures use the Windows double-click timing. The desktop returns after the second release, and ScreenOff consumes the clicks or key presses so they stay out of the app underneath. A single click, mouse movement, or holding Space keeps the blackout active.

## How it works

ScreenOff uses a dedicated Win32 desktop to isolate ordinary keyboard and mouse input and keep your regular desktop and its notifications out of view. Windows retains control of secure screens such as `Ctrl+Alt+Del`.

The display signal stays active during normal operation. An LCD may still emit some backlight even when it displays pure black.

## Build from source

Install the MSVC x64 C++ build tools and a Windows SDK through Visual Studio Build Tools. Then run from the project folder in PowerShell:

```powershell
.\build.cmd
```

The build produces `ScreenOff.exe` with the embedded Veil application icon. To run the dismissal gesture tests:

```powershell
.\build.cmd test
```
