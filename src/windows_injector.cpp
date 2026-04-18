#ifdef _WIN32

#include "windows_injector.hpp"
#include "keycode_mapping.hpp"
#include <iostream>

bool WindowsInputInjector::initialize() {
    if (initialized_) return true;
    initialized_ = true;
    std::cout << "WindowsInputInjector: initialized (SendInput)\n";
    return true;
}

void WindowsInputInjector::shutdown() {
    initialized_ = false;
}

void WindowsInputInjector::inject(const InputEvent &event) {
    if (!initialized_) return;

    switch (event.eventType) {
    case InputEvent::TYPE_MOUSE_MOVE:
        handleMouseMove(event);
        break;
    case InputEvent::TYPE_MOUSE_BUTTON:
        handleMouseButton(event);
        break;
    case InputEvent::TYPE_KEY_PRESS:
        handleKeyPress(event, true);
        break;
    case InputEvent::TYPE_KEY_RELEASE:
        handleKeyPress(event, false);
        break;
    default:
        break;
    }
}

void WindowsInputInjector::handleMouseMove(const InputEvent &event) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = event.x;
    input.mi.dy = event.y;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

void WindowsInputInjector::handleMouseButton(const InputEvent &event) {
    INPUT input = {};
    input.type = INPUT_MOUSE;

    switch (event.buttonOrKey) {
    case 0: input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
    case 1: input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
    case 2: input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; break;
    default: input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
    }

    bool pressed = !(event.flags & 0x01);
    if (!pressed) {
        // Convert DOWN flag to UP flag
        if (input.mi.dwFlags & MOUSEEVENTF_LEFTDOWN)
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        else if (input.mi.dwFlags & MOUSEEVENTF_RIGHTDOWN)
            input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        else if (input.mi.dwFlags & MOUSEEVENTF_MIDDLEDOWN)
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void WindowsInputInjector::handleKeyPress(const InputEvent &event, bool down) {
    const auto &map = KeyMapping::genericToWindows();
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;

    auto it = map.find(event.buttonOrKey);
    if (it != map.end()) {
        input.ki.wVk = static_cast<WORD>(it->second);
    } else {
        input.ki.wVk = event.buttonOrKey;
    }

    if (event.flags & InputEvent::FLAG_SHIFT) {
        INPUT shiftInput = {};
        shiftInput.type = INPUT_KEYBOARD;
        shiftInput.ki.wVk = VK_SHIFT;
        SendInput(1, &shiftInput, sizeof(INPUT));
    }

    SendInput(1, &input, sizeof(INPUT));

    if (event.flags & InputEvent::FLAG_SHIFT) {
        INPUT shiftUp = {};
        shiftUp.type = INPUT_KEYBOARD;
        shiftUp.ki.wVk = VK_SHIFT;
        shiftUp.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &shiftUp, sizeof(INPUT));
    }
}

bool WindowsInputInjector::isInitialized() const { return initialized_; }

#endif // _WIN32