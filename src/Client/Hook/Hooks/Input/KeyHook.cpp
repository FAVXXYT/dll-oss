#include "KeyHook.hpp"
#include "../../../../SDK/SDK.hpp"
#include "../../../Client.hpp"
#include "../../../../Utils/Memory/Game/SignatureAndOffsetManager.hpp"
#include "../../../../Utils/UserActionLogger.hpp"
#include "Hook/Hooks/Game/UnicodeWndProcHack.hpp"
#include "../../../Events/Input/MouseEvent.hpp"
#include "../../../../Utils/Logger/Logger.hpp"
#include "../../../GUI/Engine/EngineCore.hpp"

KeyHook::KeyHook() : Hook("KeyHook", GET_SIG_ADDRESS("Keyboard::feed"))
{
}

void KeyHook::enableHook()
{
    if (!VersionUtils::checkAboveOrEqual(21, 120))
    {
        this->autoHook((void *)keyCallback, (void **)&funcOriginal);
    }
}

void KeyHook::keyCallback(int key, bool state)
{
    keys[key] = state;

    auto event = nes::make_holder<KeyEvent>(key, state ? 1 : 0, keys);

    eventMgr.trigger(event);

    if (!event->isCancelled())
        funcOriginal(event->getKey(), state);
}

bool KeyHook::handle(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_KILLFOCUS:
    {
        // Clear all key states when the window loses focus (e.g. alt-tab)
        // so modifier keys don't get "stuck" as pressed.
        keys.fill(false);
        return false;
    }
    case WM_KEYUP:
    case WM_KEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSKEYDOWN:
    {
        bool pressed = uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN;

        if (pressed && (lParam & (1LL << 30)))
            return false; // Ignore repeats, don't block

        keys[wParam] = pressed;
        auto event = nes::make_holder<KeyEvent>(wParam, pressed, keys);
        eventMgr.trigger(event);

        return event->isCancelled();
    }
    }

    // FAVIO: MouseDevice/InputHandler sigs are dead on 1.26.52, so synthesize
    // MouseEvent from raw window messages on this same WndProc path.
    static short s_lastMouseX = 0, s_lastMouseY = 0;
    auto emitMouse = [&](char button, char action, short x, short y) {
        short dx = static_cast<short>(x - s_lastMouseX);
        short dy = static_cast<short>(y - s_lastMouseY);
        s_lastMouseX = x;
        s_lastMouseY = y;
        auto event = nes::make_holder<MouseEvent>(button, action, x, y, dx, dy);
        eventMgr.trigger(event);
        static int synthCount = 0;
        if ((++synthCount % 25) == 0) Logger::debug("[Mouse] synthesized={}", synthCount);
        return event->isCancelled();
    };
    auto msgPos = [&]() {
        return std::make_pair(static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
    };

    switch (uMsg) {
    case WM_MOUSEMOVE: {
        auto [x, y] = msgPos();
        return emitMouse(0, 0, x, y);
    }
    case WM_LBUTTONDOWN: {
        auto [x, y] = msgPos();
        return emitMouse(1, 1, x, y);
    }
    case WM_LBUTTONUP: {
        auto [x, y] = msgPos();
        return emitMouse(1, 0, x, y);
    }
    case WM_RBUTTONDOWN: {
        auto [x, y] = msgPos();
        return emitMouse(2, 1, x, y);
    }
    case WM_RBUTTONUP: {
        auto [x, y] = msgPos();
        return emitMouse(2, 0, x, y);
    }
    case WM_MBUTTONDOWN: {
        auto [x, y] = msgPos();
        return emitMouse(3, 1, x, y);
    }
    case WM_MBUTTONUP: {
        auto [x, y] = msgPos();
        return emitMouse(3, 0, x, y);
    }
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP: {
        auto [x, y] = msgPos();
        char button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 5 : 6;
        char action = (uMsg == WM_XBUTTONDOWN) ? 1 : 0;
        return emitMouse(button, action, x, y);
    }
    case WM_MOUSEWHEEL: {
        auto [x, y] = msgPos();
        char action = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 0x78 : 0x80;
        return emitMouse(4, action, x, y);
    }
    case WM_INPUT: {
        // FAVIO: hardware mouse arrives ONLY as raw input on 1.26.52 GDK
        // (no legacy messages), so synthesize menu mouse events from it.
        // Gated on open menu: when closed the game consumes raw directly and
        // a second event stream would double-count clicks.
        if (!FlarialGUI::inMenu) break;
        UINT rawSize = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &rawSize, sizeof(RAWINPUTHEADER));
        if (rawSize == 0 || rawSize > 1024) break;
        BYTE rawBuf[1024];
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, rawBuf, &rawSize, sizeof(RAWINPUTHEADER)) != rawSize) break;
        RAWINPUT* raw = (RAWINPUT*)rawBuf;
        if (raw->header.dwType != RIM_TYPEMOUSE) break;
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        short x = static_cast<short>(pt.x), y = static_cast<short>(pt.y);
        auto clampD = [](LONG d) {
            if (d > 32767) return (short)32767;
            if (d < -32768) return (short)-32768;
            return (short)d;
        };
        bool blocked = false;
        {
            short dx = clampD(raw->data.mouse.lLastX), dy = clampD(raw->data.mouse.lLastY);
            if (dx != 0 || dy != 0) {
                auto event = nes::make_holder<MouseEvent>((char)0, (char)0, x, y, dx, dy);
                eventMgr.trigger(event);
                blocked = blocked || event->isCancelled();
            }
        }
        USHORT flags = raw->data.mouse.usButtonFlags;
        auto btn = [&](char button, char action) {
            auto event = nes::make_holder<MouseEvent>(button, action, x, y, (short)0, (short)0);
            eventMgr.trigger(event);
            blocked = blocked || event->isCancelled();
        };
        if (flags & RI_MOUSE_LEFT_BUTTON_DOWN) btn(1, 1);
        if (flags & RI_MOUSE_LEFT_BUTTON_UP) btn(1, 0);
        if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN) btn(2, 1);
        if (flags & RI_MOUSE_RIGHT_BUTTON_UP) btn(2, 0);
        if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) btn(3, 1);
        if (flags & RI_MOUSE_MIDDLE_BUTTON_UP) btn(3, 0);
        if (flags & RI_MOUSE_BUTTON_4_DOWN) btn(5, 1);
        if (flags & RI_MOUSE_BUTTON_4_UP) btn(5, 0);
        if (flags & RI_MOUSE_BUTTON_5_DOWN) btn(6, 1);
        if (flags & RI_MOUSE_BUTTON_5_UP) btn(6, 0);
        if (flags & RI_MOUSE_WHEEL) {
            short delta = (short)raw->data.mouse.usButtonData;
            btn(4, delta > 0 ? (char)0x78 : (char)0x80);
        }
        return blocked;
    }
    }

    return false; // Not a key message, don't block
}
