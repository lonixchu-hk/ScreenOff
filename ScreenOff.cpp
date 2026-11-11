#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <imm.h>

class ExitGesture {
public:
    bool Handle(UINT message) {
        if (message == WM_LBUTTONDOWN || message == WM_CANCELMODE) {
            secondPress_ = false;
        } else if (message == WM_LBUTTONDBLCLK) {
            secondPress_ = true;
        } else if (message == WM_LBUTTONUP) {
            const bool dismiss = secondPress_;
            secondPress_ = false;
            return dismiss;
        }
        return false;
    }

private:
    bool secondPress_ = false;
};

class SpaceGesture {
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

    constexpr DWORD interval = 500;
    SpaceGesture space;
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

    SpaceGesture slowSpace;
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
#include <string>
#include <type_traits>
#include <vector>

namespace {
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
using Desktop = std::unique_ptr<std::remove_pointer_t<HDESK>, decltype(&CloseDesktop)>;
using KeyboardHook = std::unique_ptr<std::remove_pointer_t<HHOOK>, decltype(&UnhookWindowsHookEx)>;

constexpr wchar_t WindowClass[] = L"ScreenOffBlackout";
constexpr wchar_t DesktopPrefix[] = L"ScreenOff-";
constexpr wchar_t RestoreArgument[] = L" --restore ";
constexpr DWORD StartupTimeoutMs = 5000;
constexpr UINT DismissAfterSpace = WM_APP + 1;
constexpr ACCESS_MASK InputDesktopAccess = DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP;

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
    bool displayChanged = false;
    ExitGesture gesture;
    SpaceGesture spaceGesture;
    DWORD spaceInterval;
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
        // Windows marks mouse compatibility messages generated by a pen or touch screen.
        if ((GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700) {
            state->gesture.Handle(WM_CANCELMODE);
        } else if (state->gesture.Handle(message)) {
            SetEvent(state->dismiss);
        }
        return 0;
    case WM_CANCELMODE:
        state->gesture.Handle(message);
        return 0;
    case WM_APPCOMMAND:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
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
    if (key->vkCode == VK_SPACE && keyboardState->spaceGesture.Handle(
            (key->flags & LLKHF_UP) != 0, key->time, keyboardState->spaceInterval)) {
        // Let the hook consume the release before the worker signals the desktop switch.
        PostThreadMessageW(GetCurrentThreadId(), DismissAfterSpace, 0, 0);
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
    state.gesture.Handle(WM_CANCELMODE);
    return true;
}

void RestoreDesktop(HDESK original, HDESK blackout);

int RunWorker(HINSTANCE instance, HANDLE ready, HANDLE dismiss, HANDLE stop, HANDLE supervisor,
              const wchar_t* originalName, HDESK blackout) {
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

    WorkerState state{dismiss, false, {}, {}, GetDoubleClickTime()};
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
    if (SetEvent(ready)) {
        result = Success;
        const HANDLE exitEvents[]{stop, supervisor};
        for (;;) {
            const DWORD wait = MsgWaitForMultipleObjectsEx(2, exitEvents, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_OBJECT_0 + 1) {
                RestoreDesktop(original.get(), blackout);
                result = WorkerInterrupted;
                break;
            }
            if (wait != WAIT_OBJECT_0 + 2) {
                result = WorkerInterrupted;
                break;
            }
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == DismissAfterSpace && !message.hwnd) {
                    SetEvent(dismiss);
                } else {
                    DispatchMessageW(&message);
                }
                if (WaitForMultipleObjects(2, exitEvents, FALSE, 0) != WAIT_TIMEOUT) break;
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

    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) return WorkerLaunchFailed;
    executable.resize(length);

    wchar_t workerArgs[160]{};
    swprintf_s(workerArgs, L" --worker %ls %p %p %p %p", desktopName.c_str(), ready.get(), dismiss.get(),
               stop.get(), supervisor.get());
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
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    if (!*arguments) return Supervise();

    wchar_t expectedDesktop[64]{};
    HANDLE ready = nullptr, dismiss = nullptr, stop = nullptr, supervisor = nullptr;
    const wchar_t* originalName = std::wcsstr(arguments, RestoreArgument);
    if (!originalName || swscanf_s(arguments, L"--worker %63ls %p %p %p %p", expectedDesktop,
                                  static_cast<unsigned>(std::size(expectedDesktop)), &ready, &dismiss,
                                  &stop, &supervisor) != 5 ||
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
    return RunWorker(instance, ready, dismiss, stop, supervisor, originalName, blackout);
}
#endif
