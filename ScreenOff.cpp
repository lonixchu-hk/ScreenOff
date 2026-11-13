#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <imm.h>
#include <string>

constexpr UINT ShortcutModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT;

bool ShortcutKey(UINT key) {
    return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
           (key >= VK_F1 && key <= VK_F11);
}

bool ExitKey(UINT key) {
    return ShortcutKey(key) || key == VK_SPACE || key == VK_ESCAPE || key == VK_LCONTROL || key == VK_RETURN;
}

struct Settings {
    DWORD mouse = 1; // Off, left double, right double, left single, right single.
    DWORD key = VK_SPACE;
    DWORD shortcut = 0;

    bool Valid() const {
        const UINT modifiers = HIWORD(shortcut);
        return mouse <= 4 && (!key || ExitKey(key)) && (mouse || key) &&
               (!shortcut || (modifiers && !(modifiers & ~ShortcutModifiers) && ShortcutKey(LOWORD(shortcut))));
    }
};

std::wstring KeyName(UINT key) {
    switch (key) {
    case 0: return L"Off";
    case VK_SPACE: return L"Space";
    case VK_ESCAPE: return L"Esc";
    case VK_LCONTROL: return L"Left Ctrl";
    case VK_RETURN: return L"Enter";
    default:
        if (key >= VK_F1 && key <= VK_F11) return L"F" + std::to_wstring(key - VK_F1 + 1);
        return std::wstring(1, static_cast<wchar_t>(key));
    }
}

std::wstring ResumeHint(const Settings& settings) {
    constexpr const wchar_t* mouseHints[]{L"", L"double-click the left mouse button",
        L"double-click the right mouse button", L"click the left mouse button", L"click the right mouse button"};
    std::wstring hint = L"To resume, ";
    hint += mouseHints[settings.mouse];
    if (settings.mouse && settings.key) hint += L" or ";
    if (settings.key) hint += L"tap " + KeyName(settings.key) + L" twice";
    return hint + L".";
}

constexpr DWORD ReminderDuration = 4000;
constexpr DWORD ReminderFade = 250;
constexpr int ReminderTextGray = 208;

int ReminderGray(DWORD elapsed) {
    if (elapsed >= ReminderDuration) return 0;
    if (elapsed < ReminderFade) return ReminderTextGray * elapsed / ReminderFade;
    if (elapsed > ReminderDuration - ReminderFade) return ReminderTextGray * (ReminderDuration - elapsed) / ReminderFade;
    return ReminderTextGray;
}

class ExitGesture {
public:
    bool Handle(UINT message, bool single = false) {
        if (message == WM_LBUTTONDOWN || message == WM_CANCELMODE) {
            armed_ = single && message == WM_LBUTTONDOWN;
        } else if (message == WM_LBUTTONDBLCLK) {
            armed_ = true;
        } else if (message == WM_LBUTTONUP) {
            const bool dismiss = armed_;
            armed_ = false;
            return dismiss;
        }
        return false;
    }

private:
    bool armed_ = false;
};

class KeyGesture {
public:
    bool Handle(bool released, DWORD time, DWORD interval) {
        if (released) {
            if (state_ == State::SecondDown) {
                state_ = State::Idle;
                return true;
            }
            if (state_ == State::FirstDown) state_ = State::FirstReleased;
        } else if (state_ == State::FirstReleased && time - firstPressTime_ <= interval) {
            state_ = State::SecondDown;
        } else if (state_ == State::Idle || state_ == State::FirstReleased) {
            firstPressTime_ = time;
            state_ = State::FirstDown;
        }
        return false;
    }

private:
    enum class State { Idle, FirstDown, FirstReleased, SecondDown };
    State state_ = State::Idle;
    DWORD firstPressTime_ = 0;
};

#ifdef SCREEN_OFF_TEST
#include <cstdio>

int main() {
    if (ReminderGray(0) != 0 || ReminderGray(125) != 104 ||
        ReminderGray(250) != 208 || ReminderGray(3750) != 208 ||
        ReminderGray(3875) != 104 || ReminderGray(4000) != 0) {
        std::fputs("The reminder must fade in and out within four seconds.\n", stderr);
        return 1;
    }
    if (ResumeHint(Settings{}) != L"To resume, double-click the left mouse button or tap Space twice." ||
        ResumeHint(Settings{4, 0, 0}) != L"To resume, click the right mouse button." ||
        ResumeHint(Settings{0, VK_LCONTROL, 0}) != L"To resume, tap Left Ctrl twice.") {
        std::fputs("The reminder must show only the configured resume gestures.\n", stderr);
        return 1;
    }
    ExitGesture click;
    if (click.Handle(WM_LBUTTONDOWN) || click.Handle(WM_LBUTTONUP)) {
        std::fputs("A single click must not dismiss the blackout.\n", stderr);
        return 1;
    }
    if (click.Handle(WM_LBUTTONDBLCLK) || !click.Handle(WM_LBUTTONUP)) {
        std::fputs("A double-click must dismiss only on the second release.\n", stderr);
        return 1;
    }
    if (click.Handle(WM_LBUTTONUP)) {
        std::fputs("An unmatched release must not dismiss the blackout.\n", stderr);
        return 1;
    }

    ExitGesture ordinary;
    if (ordinary.Handle(WM_LBUTTONDOWN) || ordinary.Handle(WM_LBUTTONUP) ||
        ordinary.Handle(WM_LBUTTONDOWN) || ordinary.Handle(WM_LBUTTONUP)) {
        std::fputs("Two clicks without Windows double-click recognition must not dismiss.\n", stderr);
        return 1;
    }

    ExitGesture single;
    if (single.Handle(WM_LBUTTONUP, true) || single.Handle(WM_LBUTTONDOWN, true) ||
        !single.Handle(WM_LBUTTONUP, true) || single.Handle(WM_LBUTTONUP, true)) {
        std::fputs("Single-click mode must dismiss only on a matched release.\n", stderr);
        return 1;
    }

    Settings settings;
    if (!settings.Valid() || Settings{0, 0, 0}.Valid() || Settings{5, VK_SPACE, 0}.Valid() ||
        Settings{1, VK_SPACE, MAKELONG('B', 0)}.Valid() ||
        !Settings{0, VK_LCONTROL, MAKELONG('B', MOD_CONTROL | MOD_ALT)}.Valid()) {
        std::fputs("Settings must retain a valid exit method and reject invalid shortcuts.\n", stderr);
        return 1;
    }

    constexpr DWORD interval = 500;
    KeyGesture space;
    if (space.Handle(false, 100, interval) || space.Handle(false, 110, interval) ||
        space.Handle(true, 150, interval)) {
        std::fputs("Holding Space through repeat and releasing must not dismiss.\n", stderr);
        return 1;
    }
    if (space.Handle(false, 300, interval) || space.Handle(false, 320, interval) ||
        !space.Handle(true, 350, interval)) {
        std::fputs("Two Space taps must dismiss only on the second release.\n", stderr);
        return 1;
    }

    KeyGesture slowSpace;
    if (slowSpace.Handle(true, 50, interval) || slowSpace.Handle(false, 100, interval) ||
        slowSpace.Handle(true, 150, interval) || slowSpace.Handle(false, 601, interval) ||
        slowSpace.Handle(true, 650, interval)) {
        std::fputs("An unmatched release or slow Space pair must not dismiss.\n", stderr);
        return 1;
    }
    std::puts("Gesture tests passed.");
    return 0;
}
#else
#include <cwchar>
#include <memory>
#include <shellapi.h>
#include <type_traits>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
using Desktop = std::unique_ptr<std::remove_pointer_t<HDESK>, decltype(&CloseDesktop)>;
using KeyboardHook = std::unique_ptr<std::remove_pointer_t<HHOOK>, decltype(&UnhookWindowsHookEx)>;

constexpr wchar_t WindowClass[] = L"ScreenOffBlackout";
constexpr wchar_t DesktopPrefix[] = L"ScreenOff-";
constexpr wchar_t RestoreArgument[] = L" --restore ";
constexpr DWORD StartupTimeoutMs = 5000;
constexpr UINT DismissAfterKey = WM_APP + 1;
constexpr UINT ShowResumeHint = WM_APP + 5;
constexpr ACCESS_MASK InputDesktopAccess = DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP;
#ifdef SCREEN_OFF_DEVELOPMENT
constexpr wchar_t SettingsKey[] = L"Software\\ScreenOff-Development";
constexpr wchar_t AppName[] = L"ScreenOff Development";
constexpr wchar_t RunValue[] = L"ScreenOff-Development";
constexpr wchar_t TrayClass[] = L"ScreenOffDevelopmentShortcutListener";
constexpr wchar_t ListenerMutex[] = L"Local\\ScreenOff-Development-ShortcutListener";
#else
constexpr wchar_t SettingsKey[] = L"Software\\ScreenOff";
constexpr wchar_t AppName[] = L"ScreenOff";
constexpr wchar_t RunValue[] = L"ScreenOff";
constexpr wchar_t TrayClass[] = L"ScreenOffShortcutListener";
constexpr wchar_t ListenerMutex[] = L"Local\\ScreenOff-ShortcutListener";
#endif

Settings LoadSettings() {
    Settings settings;
    DWORD size = sizeof(settings);
    if (RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"Settings", RRF_RT_REG_BINARY,
                     nullptr, &settings, &size) != ERROR_SUCCESS || size != sizeof(settings) || !settings.Valid()) {
        return {};
    }
    return settings;
}

std::wstring ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return path;
}

enum ExitCode {
    Success,
    NoOriginalDesktop,
    DesktopCreationFailed,
    EventCreationFailed,
    WorkerLaunchFailed,
    WorkerStartupFailed,
    SwitchFailed,
    WorkerInterrupted,
    InvalidWorkerArguments,
};

struct WorkerState {
    HANDLE dismiss;
    DWORD mouse;
    DWORD key;
    DWORD interval;
    std::wstring hint;
    bool reminderRequested = false;
    int reminderGray = 0;
    bool displayChanged = false;
    ExitGesture mouseGesture;
    KeyGesture keyGesture;
};

thread_local WorkerState* keyboardState = nullptr;

LRESULT CALLBACK BlackoutWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<WorkerState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (state->reminderGray) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            const UINT dpi = GetDpiForWindow(window);
            const int margin = MulDiv(24, dpi, 96);
            HFONT font = CreateFontW(-MulDiv(14, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            const HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
            RECT text{margin, 0, bounds.right - margin, 0};
            constexpr UINT format = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX;
            DrawTextW(dc, state->hint.c_str(), -1, &text, format | DT_CALCRECT);
            const int gray = state->reminderGray;
            SetTextColor(dc, RGB(gray, gray, gray));
            SetBkMode(dc, TRANSPARENT);
            text.right = bounds.right - margin;
            OffsetRect(&text, 0, bounds.bottom * 58 / 100 - text.bottom / 2);
            DrawTextW(dc, state->hint.c_str(), -1, &text, format);
            if (font) {
                SelectObject(dc, previous);
                DeleteObject(font);
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(nullptr);
        return TRUE;
    case WM_DISPLAYCHANGE:
        state->displayChanged = true;
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP: {
        const bool right = message >= WM_RBUTTONDOWN;
        if (!state->mouse || right != (state->mouse % 2 == 0)) {
            if (message == WM_LBUTTONUP || message == WM_RBUTTONUP) state->reminderRequested = true;
            return 0;
        }
        const UINT click = right ? message - (WM_RBUTTONDOWN - WM_LBUTTONDOWN) : message;
        // Windows marks mouse compatibility messages generated by a pen or touch screen.
        if ((GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700) {
            state->mouseGesture.Handle(WM_CANCELMODE);
            if (click == WM_LBUTTONUP) state->reminderRequested = true;
        } else if (state->mouseGesture.Handle(click, state->mouse >= 3)) {
            SetEvent(state->dismiss);
        } else if (click == WM_LBUTTONUP) {
            state->reminderRequested = true;
        }
        return 0;
    }
    case WM_CANCELMODE:
        state->mouseGesture.Handle(message);
        return 0;
    case WM_APPCOMMAND:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        return TRUE;
    case WM_XBUTTONUP:
    case WM_MBUTTONUP:
        state->reminderRequested = true;
        return TRUE;
    case WM_CLOSE:
    case WM_SYSCOMMAND:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CONTEXTMENU:
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

LRESULT CALLBACK ConsumeKeyboard(int code, WPARAM wparam, LPARAM lparam) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wparam, lparam);

    const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
    const bool released = (key->flags & LLKHF_UP) != 0;
    if (keyboardState->key && key->vkCode == keyboardState->key && keyboardState->keyGesture.Handle(
            released, key->time, keyboardState->interval)) {
        // Let the hook consume the release before the worker signals the desktop switch.
        PostThreadMessageW(GetCurrentThreadId(), DismissAfterKey, 0, 0);
    } else if (released) {
        PostThreadMessageW(GetCurrentThreadId(), ShowResumeHint, 0, 0);
    }
    return 1;
}

BOOL CALLBACK CollectMonitor(HMONITOR, HDC, LPRECT bounds, LPARAM data) {
    reinterpret_cast<std::vector<RECT>*>(data)->push_back(*bounds);
    return TRUE;
}

bool UpdateWindows(HINSTANCE instance, WorkerState& state, std::vector<HWND>& windows) {
    std::vector<RECT> monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors)) ||
        monitors.empty()) {
        return false;
    }

    std::vector<HWND> replacements;
    replacements.reserve(monitors.size());
    for (const RECT& bounds : monitors) {
        HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, WindowClass, L"Screen Off", WS_POPUP,
                                      bounds.left, bounds.top, bounds.right - bounds.left,
                                      bounds.bottom - bounds.top, nullptr, nullptr, instance, &state);
        if (!window) {
            for (HWND replacement : replacements) DestroyWindow(replacement);
            return false;
        }
        replacements.push_back(window);
    }
    for (HWND window : replacements) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
    SetActiveWindow(replacements.front());
    for (HWND window : windows) DestroyWindow(window);
    windows.swap(replacements);
    state.mouseGesture.Handle(WM_CANCELMODE);
    return true;
}

void RestoreDesktop(HDESK original, HDESK blackout);

int RunWorker(HINSTANCE instance, HANDLE ready, HANDLE dismiss, HANDLE stop, HANDLE supervisor,
              const wchar_t* originalName, HDESK blackout, const Settings& settings) {
    Desktop original(OpenDesktopW(originalName, 0, FALSE, InputDesktopAccess), CloseDesktop);
    if (!original || !SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return WorkerStartupFailed;
    }

    // The blackout has no text input. Suppress the input-method UI on this thread.
    ImmDisableIME(GetCurrentThreadId());

    WNDCLASSW windowClass{};
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = BlackoutWindow;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = WindowClass;
    if (!RegisterClassW(&windowClass)) return WorkerStartupFailed;

    WorkerState state{dismiss, settings.mouse, settings.key, GetDoubleClickTime(), ResumeHint(settings)};
    std::vector<HWND> windows;
    if (!UpdateWindows(instance, state, windows)) return WorkerStartupFailed;
    SetCursor(nullptr);

    // Low-level hooks are scoped to the calling thread's desktop. The original desktop has no hook.
    keyboardState = &state;
    KeyboardHook keyboard(SetWindowsHookExW(WH_KEYBOARD_LL, ConsumeKeyboard, instance, 0), UnhookWindowsHookEx);
    if (!keyboard || !SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED)) {
        return WorkerStartupFailed;
    }

    int result = WorkerStartupFailed;
    bool reminderActive = false;
    DWORD reminderStart = 0;
    if (SetEvent(ready)) {
        result = Success;
        const HANDLE exitEvents[]{stop, supervisor};
        for (;;) {
            const DWORD wait = MsgWaitForMultipleObjectsEx(2, exitEvents, reminderActive ? 16 : INFINITE,
                                                          QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_OBJECT_0 + 1) {
                RestoreDesktop(original.get(), blackout);
                result = WorkerInterrupted;
                break;
            }
            if (wait != WAIT_OBJECT_0 + 2 && wait != WAIT_TIMEOUT) {
                result = WorkerInterrupted;
                break;
            }
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == DismissAfterKey && !message.hwnd) {
                    SetEvent(dismiss);
                } else if (message.message == ShowResumeHint && !message.hwnd) {
                    state.reminderRequested = true;
                } else {
                    DispatchMessageW(&message);
                }
                if (WaitForMultipleObjects(2, exitEvents, FALSE, 0) != WAIT_TIMEOUT) break;
            }
            if (state.reminderRequested && !reminderActive) {
                reminderStart = GetTickCount();
                reminderActive = true;
            }
            state.reminderRequested = false;
            if (reminderActive) {
                const DWORD elapsed = GetTickCount() - reminderStart;
                const int gray = ReminderGray(elapsed);
                if (gray != state.reminderGray) {
                    state.reminderGray = gray;
                    for (HWND window : windows) RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                }
                reminderActive = elapsed < ReminderDuration;
            }
            if (state.displayChanged) {
                state.displayChanged = false;
                if (!UpdateWindows(instance, state, windows)) {
                    result = WorkerInterrupted;
                    break;
                }
            }
        }
    }

    SetThreadExecutionState(ES_CONTINUOUS);
    for (HWND window : windows) DestroyWindow(window);
    return result;
}

bool HasInput(HDESK desktop) {
    BOOL active = FALSE;
    return GetUserObjectInformationW(desktop, UOI_IO, &active, sizeof(active), nullptr) && active;
}

void RestoreDesktop(HDESK original, HDESK blackout) {
    // Give Winlogon priority if Windows has switched to a secure desktop.
    while (!HasInput(original)) {
        if (HasInput(blackout) && SwitchDesktop(original)) return;
        Sleep(100);
    }
}

int Supervise() {
    Handle instanceLock(CreateMutexW(nullptr, FALSE, L"Local\\ScreenOff-MVP"), CloseHandle);
    const DWORD lockError = GetLastError();
    if (!instanceLock) return EventCreationFailed;
    if (lockError == ERROR_ALREADY_EXISTS) return Success;

    Desktop original(OpenInputDesktop(0, FALSE, InputDesktopAccess), CloseDesktop);
    if (!original || !HasInput(original.get())) return NoOriginalDesktop;
    wchar_t originalName[256]{};
    if (!GetUserObjectInformationW(original.get(), UOI_NAME, originalName, sizeof(originalName), nullptr)) {
        return NoOriginalDesktop;
    }

    std::wstring desktopName = DesktopPrefix + std::to_wstring(GetCurrentProcessId());
    Desktop blackout(CreateDesktopW(desktopName.c_str(), nullptr, nullptr, 0,
                                    InputDesktopAccess | DESKTOP_CREATEWINDOW, nullptr), CloseDesktop);
    if (!blackout) return DesktopCreationFailed;

    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    Handle ready(CreateEventW(&inherit, TRUE, FALSE, nullptr), CloseHandle);
    Handle dismiss(CreateEventW(&inherit, TRUE, FALSE, nullptr), CloseHandle);
    Handle stop(CreateEventW(&inherit, TRUE, FALSE, nullptr), CloseHandle);
    if (!ready || !dismiss || !stop) return EventCreationFailed;
    HANDLE supervisorHandle = nullptr;
    const HANDLE self = GetCurrentProcess();
    if (!DuplicateHandle(self, self, self, &supervisorHandle, SYNCHRONIZE, TRUE, 0)) return EventCreationFailed;
    Handle supervisor(supervisorHandle, CloseHandle);

    const std::wstring executable = ExecutablePath();
    if (executable.empty()) return WorkerLaunchFailed;

    const Settings settings = LoadSettings();
    wchar_t workerArgs[160]{};
    swprintf_s(workerArgs, L" --worker %ls %p %p %p %p %lu %lu", desktopName.c_str(), ready.get(), dismiss.get(),
               stop.get(), supervisor.get(), settings.mouse, settings.key);
    std::wstring command = L"\"" + executable + L"\"" + workerArgs + RestoreArgument + originalName;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.lpDesktop = desktopName.data();
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &startup, &child)) {
        return WorkerLaunchFailed;
    }
    Handle worker(child.hProcess, CloseHandle);
    CloseHandle(child.hThread);

    const HANDLE startupEvents[]{worker.get(), ready.get()};
    int result = WorkerStartupFailed;
    if (WaitForMultipleObjects(2, startupEvents, FALSE, StartupTimeoutMs) == WAIT_OBJECT_0 + 1) {
        result = SwitchFailed;
        if (HasInput(original.get()) && WaitForSingleObject(worker.get(), 0) == WAIT_TIMEOUT &&
            SwitchDesktop(blackout.get())) {
            const HANDLE exitEvents[]{worker.get(), dismiss.get()};
            DWORD exitSignal;
            do {
                exitSignal = WaitForMultipleObjects(2, exitEvents, FALSE, 250);
            } while (exitSignal == WAIT_TIMEOUT && !HasInput(original.get()));
            RestoreDesktop(original.get(), blackout.get());
            result = (exitSignal == WAIT_OBJECT_0 + 1 || exitSignal == WAIT_TIMEOUT) ? Success : WorkerInterrupted;
        }
    }
    SetEvent(stop.get());
    if (WaitForSingleObject(worker.get(), StartupTimeoutMs) == WAIT_TIMEOUT) {
        TerminateProcess(worker.get(), WorkerInterrupted);
        WaitForSingleObject(worker.get(), INFINITE);
    }
    return result;
}

constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr UINT OpenSettings = WM_APP + 2;
constexpr UINT TrayClick = WM_APP + 3;
constexpr UINT ShortcutRecorded = WM_APP + 4;
enum { SettingsDialog = 200, MouseChoice, KeyChoice, ShortcutText, RecordShortcut, ClearShortcut, StartAtSignIn };

struct Listener {
    HINSTANCE instance;
    Settings settings = LoadSettings();
    HWND window = nullptr;
    HWND dialog = nullptr;
    DWORD registered = 0;
    int hotkeyId = 1;
    DWORD draftShortcut = 0;
    DWORD captured = 0;
    KeyboardHook recorder{nullptr, UnhookWindowsHookEx};
    bool pending = false;
    UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    Handle blackout{nullptr, CloseHandle};
};

thread_local Listener* captureState = nullptr;

void ShowShortcut(Listener& state) {
    std::wstring label;
    const UINT mods = HIWORD(state.draftShortcut);
    if (mods & MOD_CONTROL) label += L"Ctrl + ";
    if (mods & MOD_ALT) label += L"Alt + ";
    if (mods & MOD_SHIFT) label += L"Shift + ";
    label += KeyName(LOWORD(state.draftShortcut));
    SetDlgItemTextW(state.dialog, ShortcutText, label.c_str());
    EnableWindow(GetDlgItem(state.dialog, StartAtSignIn), state.draftShortcut != 0);
}

bool SignInEnabled() {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, RunKey, RunValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
}

bool SetSignIn(bool enabled) {
    if (!enabled) {
        const LSTATUS result = RegDeleteKeyValueW(HKEY_CURRENT_USER, RunKey, RunValue);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    const auto path = ExecutablePath();
    const auto command = L"\"" + path + L"\" --tray";
    return !path.empty() && command.size() < 260 &&
           RegSetKeyValueW(HKEY_CURRENT_USER, RunKey, RunValue, REG_SZ, command.c_str(),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool RegisterShortcut(HWND window, int id, DWORD shortcut) {
    return RegisterHotKey(window, id, HIWORD(shortcut) | MOD_NOREPEAT, LOWORD(shortcut)) != FALSE;
}

const wchar_t* SaveSettings(Listener& state, const Settings& next) {
    if (!next.Valid()) return L"Enable at least one exit method.";
    const bool changed = next.shortcut != state.registered;
    const int candidate = 3 - state.hotkeyId;
    if (changed && next.shortcut && !RegisterShortcut(state.window, candidate, next.shortcut)) {
        return L"Windows could not register this shortcut. Choose another combination.";
    }
    if (RegSetKeyValueW(HKEY_CURRENT_USER, SettingsKey, L"Settings", REG_BINARY, &next, sizeof(next)) != ERROR_SUCCESS) {
        if (changed && next.shortcut) UnregisterHotKey(state.window, candidate);
        return L"ScreenOff could not save the settings.";
    }
    if (changed) {
        if (state.registered) UnregisterHotKey(state.window, state.hotkeyId);
        state.hotkeyId = candidate;
        state.registered = next.shortcut;
    }
    state.settings = next;
    return nullptr;
}

LRESULT CALLBACK CaptureShortcut(int code, WPARAM message, LPARAM data) {
    auto& state = *captureState;
    if (code == HC_ACTION && GetForegroundWindow() == state.dialog) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if (!state.captured && !(key->flags & LLKHF_UP) && GetAsyncKeyState(static_cast<int>(key->vkCode)) >= 0) {
            const UINT modifiers = (GetAsyncKeyState(VK_CONTROL) < 0 ? MOD_CONTROL : 0) |
                                   (GetAsyncKeyState(VK_MENU) < 0 ? MOD_ALT : 0) |
                                   (GetAsyncKeyState(VK_SHIFT) < 0 ? MOD_SHIFT : 0);
            if (key->vkCode == VK_ESCAPE) state.captured = VK_ESCAPE;
            else if (modifiers && ShortcutKey(key->vkCode) && GetAsyncKeyState(VK_LWIN) >= 0 &&
                     GetAsyncKeyState(VK_RWIN) >= 0) state.captured = MAKELONG(key->vkCode, modifiers);
        }
        if (state.captured && key->vkCode == LOWORD(state.captured)) {
            if (key->flags & LLKHF_UP) PostMessageW(state.dialog, ShortcutRecorded, state.captured, 0);
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

INT_PTR CALLBACK SettingsProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INITDIALOG) {
#ifdef SCREEN_OFF_DEVELOPMENT
        SetWindowTextW(dialog, L"ScreenOff Development Settings");
#endif
        auto& state = *reinterpret_cast<Listener*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        state.dialog = dialog;
        state.draftShortcut = state.settings.shortcut;
        const wchar_t* const choices[]{L"Off", L"Left double-click", L"Right double-click", L"Left single-click", L"Right single-click"};
        for (const auto* choice : choices) {
            SendDlgItemMessageW(dialog, MouseChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice));
        }
        SendDlgItemMessageW(dialog, MouseChoice, CB_SETCURSEL, state.settings.mouse, 0);
        for (UINT key = 0; key <= VK_LCONTROL; ++key) {
            if (key && !ExitKey(key)) continue;
            const auto name = KeyName(key);
            const auto index = SendDlgItemMessageW(dialog, KeyChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            SendDlgItemMessageW(dialog, KeyChoice, CB_SETITEMDATA, index, key);
            if (key == state.settings.key) SendDlgItemMessageW(dialog, KeyChoice, CB_SETCURSEL, index, 0);
        }
        CheckDlgButton(dialog, StartAtSignIn, SignInEnabled() ? BST_CHECKED : BST_UNCHECKED);
        ShowShortcut(state);
        return TRUE;
    }
    if (message != WM_COMMAND && message != ShortcutRecorded) return FALSE;
    auto& state = *reinterpret_cast<Listener*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == ShortcutRecorded) {
        if (state.recorder && HIWORD(wparam)) state.draftShortcut = static_cast<DWORD>(wparam);
        state.recorder.reset();
        ShowShortcut(state);
        return TRUE;
    }
    switch (LOWORD(wparam)) {
    case ShortcutText:
        if (HIWORD(wparam) == EN_KILLFOCUS && state.recorder) {
            state.recorder.reset();
            ShowShortcut(state);
        }
        return TRUE;
    case RecordShortcut:
        state.captured = 0;
        captureState = &state;
        if (!state.recorder) state.recorder.reset(SetWindowsHookExW(WH_KEYBOARD_LL, CaptureShortcut, state.instance, 0));
        if (!state.recorder) {
            MessageBoxW(dialog, L"ScreenOff could not start shortcut recording.", L"Setting", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        SetDlgItemTextW(dialog, ShortcutText, L"Press a shortcut, or Esc to cancel");
        SetFocus(GetDlgItem(dialog, ShortcutText));
        return TRUE;
    case ClearShortcut:
        state.draftShortcut = 0;
        CheckDlgButton(dialog, StartAtSignIn, BST_UNCHECKED);
        ShowShortcut(state);
        return TRUE;
    case IDOK: {
        if (state.recorder) return TRUE;
        const auto mouse = SendDlgItemMessageW(dialog, MouseChoice, CB_GETCURSEL, 0, 0);
        const auto index = SendDlgItemMessageW(dialog, KeyChoice, CB_GETCURSEL, 0, 0);
        const auto key = SendDlgItemMessageW(dialog, KeyChoice, CB_GETITEMDATA, index, 0);
        const Settings next{static_cast<DWORD>(mouse), static_cast<DWORD>(key), state.draftShortcut};
        if (const auto* error = SaveSettings(state, next)) {
            MessageBoxW(dialog, error, L"Setting", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        const bool signIn = next.shortcut && IsDlgButtonChecked(dialog, StartAtSignIn) == BST_CHECKED;
        if (signIn != SignInEnabled() && !SetSignIn(signIn)) {
            MessageBoxW(dialog, L"Settings were saved, but Windows could not update Start at sign-in.",
                        L"Setting", MB_OK | MB_ICONWARNING);
            CheckDlgButton(dialog, StartAtSignIn, SignInEnabled() ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    case IDCANCEL:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

void TrayIcon(const Listener& state, DWORD action) {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = state.window;
    icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon.uCallbackMessage = TrayClick;
    icon.hIcon = LoadIconW(state.instance, MAKEINTRESOURCEW(101));
    wcscpy_s(icon.szTip, AppName);
    Shell_NotifyIconW(action, &icon);
}

void ShowSettings(Listener& state) {
    if (state.dialog) {
        SetForegroundWindow(state.dialog);
        return;
    }
    KillTimer(state.window, 1);
    state.pending = false;
    DialogBoxParamW(state.instance, MAKEINTRESOURCEW(SettingsDialog), nullptr, SettingsProc,
                    reinterpret_cast<LPARAM>(&state));
    state.dialog = nullptr;
    state.recorder.reset();
    if (!state.registered) DestroyWindow(state.window);
    else TrayIcon(state, NIM_ADD);
}

bool ShortcutHeld(DWORD shortcut) {
    const UINT mods = HIWORD(shortcut);
    return GetAsyncKeyState(LOWORD(shortcut)) < 0 ||
           ((mods & MOD_CONTROL) && GetAsyncKeyState(VK_CONTROL) < 0) ||
           ((mods & MOD_ALT) && GetAsyncKeyState(VK_MENU) < 0) ||
           ((mods & MOD_SHIFT) && GetAsyncKeyState(VK_SHIFT) < 0);
}

LRESULT CALLBACK TrayProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<Listener*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case OpenSettings:
        ShowSettings(*state);
        return 0;
    case WM_HOTKEY:
        if (!state->dialog && wparam == static_cast<WPARAM>(state->hotkeyId) &&
            state->registered == static_cast<DWORD>(MAKELONG(HIWORD(lparam), LOWORD(lparam)))) {
            state->pending = SetTimer(window, 1, 20, nullptr) != 0;
        }
        return 0;
    case WM_TIMER: {
        if (wparam != 1 || !state->pending) return 0;
        if (!HasInput(GetThreadDesktop(GetCurrentThreadId())) || !ShortcutHeld(state->registered)) {
            KillTimer(window, 1);
            state->pending = false;
            if (state->dialog || !HasInput(GetThreadDesktop(GetCurrentThreadId())) ||
                (state->blackout && WaitForSingleObject(state->blackout.get(), 0) == WAIT_TIMEOUT)) return 0;
            const auto executable = ExecutablePath();
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION child{};
            if (!executable.empty() && CreateProcessW(executable.c_str(), nullptr, nullptr, nullptr, FALSE, 0,
                                                       nullptr, nullptr, &startup, &child)) {
                state->blackout.reset(child.hProcess);
                CloseHandle(child.hThread);
            }
        }
        return 0;
    }
    case TrayClick:
        if (lparam == WM_LBUTTONDBLCLK) ShowSettings(*state);
        if (lparam == WM_RBUTTONUP && !state->dialog) {
            const HMENU menu = CreatePopupMenu();
            if (!menu) return 0;
            AppendMenuW(menu, MF_STRING, 1, L"Setting");
            AppendMenuW(menu, MF_STRING, 2, L"Exit shortcut listener");
            POINT cursor{};
            GetCursorPos(&cursor);
            SetForegroundWindow(window);
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, window, nullptr);
            DestroyMenu(menu);
            PostMessageW(window, WM_NULL, 0, 0);
            if (command == 1) ShowSettings(*state);
            if (command == 2) DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        if (state->registered) UnregisterHotKey(window, state->hotkeyId);
        TrayIcon(*state, NIM_DELETE);
        PostQuitMessage(0);
        return 0;
    default:
        if (state && state->registered && message == state->taskbarCreated) TrayIcon(*state, NIM_ADD);
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

int RunListener(HINSTANCE instance, bool configure) {
    Handle lock(CreateMutexW(nullptr, FALSE, ListenerMutex), CloseHandle);
    const DWORD lockError = GetLastError();
    if (!lock) return EventCreationFailed;
    if (lockError == ERROR_ALREADY_EXISTS) {
        for (int attempt = 0; configure && attempt < 50; ++attempt) {
            if (const HWND window = FindWindowW(TrayClass, nullptr)) {
                DWORD process = 0;
                GetWindowThreadProcessId(window, &process);
                AllowSetForegroundWindow(process);
                PostMessageW(window, OpenSettings, 0, 0);
                break;
            }
            Sleep(20);
        }
        return Success;
    }
    Listener state{instance};
    if (!configure && !state.settings.shortcut) return Success;
    WNDCLASSW type{};
    type.lpfnWndProc = TrayProc;
    type.hInstance = instance;
    type.lpszClassName = TrayClass;
    if (!RegisterClassW(&type)) return WorkerStartupFailed;
    state.window = CreateWindowExW(WS_EX_TOOLWINDOW, TrayClass, L"ScreenOff Shortcut", WS_POPUP,
                                  0, 0, 0, 0, nullptr, nullptr, instance, &state);
    if (!state.window) return WorkerStartupFailed;
    if (state.settings.shortcut && RegisterShortcut(state.window, state.hotkeyId, state.settings.shortcut)) {
        state.registered = state.settings.shortcut;
        TrayIcon(state, NIM_ADD);
    } else if (!configure) {
        MessageBoxW(nullptr, L"Windows could not register the saved shortcut. Open Setting to change it.",
                    L"ScreenOff", MB_OK | MB_ICONWARNING);
        DestroyWindow(state.window);
        return Success;
    }
    if (configure) ShowSettings(state);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
    return Success;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    if (!*arguments) return Supervise();
    if (std::wcscmp(arguments, L"--settings") == 0) return RunListener(instance, true);
    if (std::wcscmp(arguments, L"--tray") == 0) return RunListener(instance, false);

    wchar_t expectedDesktop[64]{};
    HANDLE ready = nullptr, dismiss = nullptr, stop = nullptr, supervisor = nullptr;
    Settings settings;
    const wchar_t* originalName = std::wcsstr(arguments, RestoreArgument);
    if (!originalName || swscanf_s(arguments, L"--worker %63ls %p %p %p %p %lu %lu", expectedDesktop,
                                  static_cast<unsigned>(std::size(expectedDesktop)), &ready, &dismiss,
                                  &stop, &supervisor, &settings.mouse, &settings.key) != 7 || !settings.Valid() ||
        std::wcsncmp(expectedDesktop, DesktopPrefix, std::size(DesktopPrefix) - 1) != 0) {
        return InvalidWorkerArguments;
    }
    originalName += std::size(RestoreArgument) - 1;
    wchar_t currentDesktop[64]{};
    DWORD flags = 0;
    const HDESK blackout = GetThreadDesktop(GetCurrentThreadId());
    if (!GetUserObjectInformationW(blackout, UOI_NAME, currentDesktop, sizeof(currentDesktop), nullptr) ||
        std::wcscmp(expectedDesktop, currentDesktop) != 0 ||
        !*originalName || std::wcscmp(originalName, currentDesktop) == 0 ||
        !GetHandleInformation(ready, &flags) || !GetHandleInformation(dismiss, &flags) ||
        !GetHandleInformation(stop, &flags) || !GetHandleInformation(supervisor, &flags)) {
        return InvalidWorkerArguments;
    }
    return RunWorker(instance, ready, dismiss, stop, supervisor, originalName, blackout, settings);
}
#endif
