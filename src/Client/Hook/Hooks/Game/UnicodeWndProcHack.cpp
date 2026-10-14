// UnicodeWndProcHack.cpp
#include "UnicodeWndProcHack.hpp"

#include "Hook/Hooks/Input/KeyHook.hpp"
#include "../../../../Utils/Logger/Logger.hpp"
#include "../../../GUI/Engine/EngineCore.hpp"

#include <map>
#include <utility>

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

    // Diagnostic: prove which hooked windows real mouse traffic reaches.
    // (One line per window, once.) Absence of traffic here while the game
    // visibly reacts means input bypasses window messages (raw/GameInput).
    {
        static std::map<HWND, std::pair<long long, long long>> traffic; // moves, clicks
        static std::map<HWND, long long> rawTraffic; // WM_INPUT (raw/hardware)
        static bool reported = false;
        bool countIt = false;
        bool isClick = false;
        switch (uMsg) {
        case WM_MOUSEMOVE: countIt = true; break;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            countIt = true; isClick = true; break;
        case WM_INPUT:
            rawTraffic[hWnd]++;
            if (!reported) {
                long long total = 0;
                for (auto &kv : traffic) total += kv.second.first + kv.second.second;
                for (auto &kv : rawTraffic) total += kv.second;
                if (total >= 300) {
                    reported = true;
                    for (auto &kv : traffic)
                        Logger::debug("[WndProc] traffic hwnd=0x{:X} moves={} clicks={}",
                            (unsigned long long)(uintptr_t)kv.first, kv.second.first, kv.second.second);
                    for (auto &kv : rawTraffic)
                        Logger::debug("[WndProc] raw hwnd=0x{:X} count={}",
                            (unsigned long long)(uintptr_t)kv.first, kv.second);
                }
            }
            break;
        default: break;
        }
        if (countIt && !reported) {
            auto &slot = traffic[hWnd];
            if (isClick) slot.second++; else slot.first++;
            long long total = 0;
            for (auto &kv : traffic) total += kv.second.first + kv.second.second;
            if (total >= 300) {
                reported = true;
                for (auto &kv : traffic)
                    Logger::debug("[WndProc] traffic hwnd=0x{:X} moves={} clicks={}",
                        (unsigned long long)(uintptr_t)kv.first, kv.second.first, kv.second.second);
            }
        }
    }

    // Check if key event was cancelled (e.g., when GUI is open)
    if (KeyHook::handle(hWnd, uMsg, wParam, lParam))
        return 0; // Block the key from reaching the game

    // Modal menu: with the menu open the game must not see the mouse at all
    // (hover highlights, clicks on inventory/settings, look-around deltas that
    // arrive as messages). The menu already got its copy via handle() above,
    // so swallowing here only starves the game. Keys intentionally pass through
    // so typing in menu textboxes keeps working.
    if (FlarialGUI::inMenu) {
        switch (uMsg) {
        case WM_MOUSEMOVE:
        case WM_INPUT:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            return 0;
        default: break;
        }
    }

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
