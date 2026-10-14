#include "KeyHook.hpp"
#include "../../../../SDK/SDK.hpp"
#include "../../../Client.hpp"
#include "../../../../Utils/Memory/Game/SignatureAndOffsetManager.hpp"
#include "../../../../Utils/UserActionLogger.hpp"
#include "Hook/Hooks/Game/UnicodeWndProcHack.hpp"
#include "../../../Events/Input/MouseEvent.hpp"

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

    // FAVIO: mouse buttons/movement come ONLY from the per-frame poll in
    // DX12Render (see single-stream note above); the legacy path carries
    // ONLY the wheel (which cannot be polled).
    switch (uMsg) {
    case WM_MOUSEWHEEL: {
        short x = static_cast<short>(LOWORD(lParam)), y = static_cast<short>(HIWORD(lParam));
        char action = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 0x78 : 0x80;
        auto event = nes::make_holder<MouseEvent>((char)4, action, x, y, (short)0, (short)0);
        eventMgr.trigger(event);
        return event->isCancelled();
    }
    case WM_MOUSEHWHEEL: {
        return true;
    }
    }

    return false; // Not a key message, don't block
}
