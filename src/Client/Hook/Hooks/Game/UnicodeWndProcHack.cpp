// UnicodeWndProcHack.cpp
#include "UnicodeWndProcHack.hpp"

#include "Hook/Hooks/Input/KeyHook.hpp"

#include <map>

WNDPROC UnicodeWndProcHack::g_realWndProc = nullptr;

// Per-window original procs. GDK input may go to a different HWND than the
// "Bedrock"-class host, so every suitable window of our process gets covered.
static std::map<HWND, WNDPROC> g_wndProcs;

static void hookOneWindow(HWND hWnd) {
    if (!hWnd || g_wndProcs.count(hWnd)) return;
    if (GetParent(hWnd) == HWND_MESSAGE) return; // message-only: no input
    wchar_t cls[64] = {};
    if (GetClassNameW(hWnd, cls, 64) && wcscmp(cls, L"IME") == 0) return;
    WNDPROC cur = (WNDPROC)GetWindowLongPtrW(hWnd, GWLP_WNDPROC);
    if (cur == UnicodeWndProcHack::MasterWndProc) { g_wndProcs[hWnd] = nullptr; return; }
    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)UnicodeWndProcHack::MasterWndProc);
    if (!prev) return; // cannot subclass (e.g. foreign privilege) - skip
    g_wndProcs[hWnd] = (prev == UnicodeWndProcHack::MasterWndProc) ? nullptr : prev;
    if (!UnicodeWndProcHack::g_realWndProc && g_wndProcs[hWnd])
        UnicodeWndProcHack::g_realWndProc = g_wndProcs[hWnd];
}

static BOOL CALLBACK hookChildren(HWND hWnd, LPARAM pid) {
    DWORD wpid = 0;
    GetWindowThreadProcessId(hWnd, &wpid);
    if (wpid == (DWORD)pid) hookOneWindow(hWnd);
    return TRUE;
}

static BOOL CALLBACK hookTopLevel(HWND hWnd, LPARAM pid) {
    DWORD wpid = 0;
    GetWindowThreadProcessId(hWnd, &wpid);
    if (wpid != (DWORD)pid) return TRUE;
    hookOneWindow(hWnd);
    EnumChildWindows(hWnd, hookChildren, pid);
    return TRUE;
}

static void ensureAllWindowsHooked() {
    EnumWindows(hookTopLevel, (LPARAM)GetCurrentProcessId());
}

bool UnicodeWndProcHack::handle(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_SETTEXT && !IsWindowUnicode(hWnd))
        return true;
    return false;
}

LRESULT UnicodeWndProcHack::MasterWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_SETTEXT && !IsWindowUnicode(hWnd))
        return TRUE;

    // Late-created windows (e.g. after mode switches): rescan periodically.
    static int msgCount = 0;
    if ((++msgCount % 500) == 0) ensureAllWindowsHooked();

    // Check if key event was cancelled (e.g., when GUI is open)
    if (KeyHook::handle(hWnd, uMsg, wParam, lParam))
        return 0; // Block the key from reaching the game

    // Dispatch to this window's original proc (multi-window coverage).
    auto it = g_wndProcs.find(hWnd);
    WNDPROC real = (it != g_wndProcs.end()) ? it->second : nullptr;
    if (!real) real = g_realWndProc;
    if (!real || real == MasterWndProc)
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);

    return CallWindowProcW(real, hWnd, uMsg, wParam, lParam);
}

void UnicodeWndProcHack::enableHook()
{
    ensureAllWindowsHooked();
    if (!g_realWndProc && Client::window)
    {
        WNDPROC currentProc = (WNDPROC)GetWindowLongPtrW(Client::window, GWLP_WNDPROC);

        // Don't hook if already hooked (prevents recursive hooks)
        if (currentProc == MasterWndProc)
            return;

        g_realWndProc = (WNDPROC)SetWindowLongPtrW(Client::window, GWLP_WNDPROC, (LONG_PTR)MasterWndProc);

        // Verify we didn't hook ourselves
        if (g_realWndProc == MasterWndProc)
            g_realWndProc = nullptr;
    }
}
