<p align="center">
  <img src="assets/screenoff.png" width="112" height="112" alt="ScreenOff Veil logo">
</p>

<h1 align="center">ScreenOff</h1>

<p align="center">Black out every monitor while your apps keep running.</p>

<p align="center">
  <a href="#use-screenoff"><img src="https://img.shields.io/badge/Windows-11%20x64-0078D4" alt="Windows 11 x64"></a>
  <a href="#build-from-source"><img src="https://img.shields.io/badge/Win32-C%2B%2B17-5B21B6" alt="Win32 and C++17"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-15803D" alt="MIT license"></a>
</p>

## About ScreenOff

ScreenOff is a small, portable Windows app for when you want a dark screen without interrupting work in the background. It fills every active display with pure black (`#000000`), hides the pointer, and asks Windows to keep the displays awake during the blackout.

## Use ScreenOff

Run [ScreenOff.exe](ScreenOff.exe) on Windows 11 x64 to start the blackout immediately. To return to your desktop, **double-click the left mouse button** or **tap Space twice quickly**.

Both gestures use the Windows double-click timing. The desktop returns after the second release, and ScreenOff consumes the clicks or key presses so they stay out of the app underneath. A single click, mouse movement, or holding Space keeps the blackout active.

## How it works

ScreenOff uses a dedicated Win32 desktop to isolate ordinary keyboard and mouse input and keep your regular desktop and its notifications out of view. Windows retains control of secure screens such as `Ctrl+Alt+Del`.

The display signal stays active during normal operation. An LCD may still emit some backlight even when it displays pure black.

## Build from source

Install the MSVC x64 C++ build tools and a Windows SDK through Visual Studio Build Tools. Run `build.cmd` from the project folder in PowerShell.

```powershell
.\build.cmd
```

The build produces `ScreenOff.exe` with the embedded Veil application icon. Run the gesture tests with `build.cmd test`.

```powershell
.\build.cmd test
```

## License

ScreenOff is available under the [MIT License](LICENSE).
