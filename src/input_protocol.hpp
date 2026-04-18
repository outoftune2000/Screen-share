#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#pragma pack(push, 1)
struct InputEvent {
    uint8_t eventType;   // 0=MouseMove, 1=MouseButton, 2=KeyPress, 3=KeyRelease
    uint8_t buttonOrKey; // OS-agnostic mapped keycode or mouse button ID
    int16_t x;            // X coordinate (absolute or relative)
    int16_t y;            // Y coordinate (absolute or relative)
    uint8_t flags;        // Modifiers (Ctrl, Alt, Shift)

    static constexpr uint8_t TYPE_MOUSE_MOVE = 0;
    static constexpr uint8_t TYPE_MOUSE_BUTTON = 1;
    static constexpr uint8_t TYPE_KEY_PRESS = 2;
    static constexpr uint8_t TYPE_KEY_RELEASE = 3;

    static constexpr uint8_t FLAG_CTRL = 1;
    static constexpr uint8_t FLAG_ALT = 2;
    static constexpr uint8_t FLAG_SHIFT = 4;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf(sizeof(InputEvent));
        std::memcpy(buf.data(), this, sizeof(InputEvent));
        return buf;
    }

    static InputEvent deserialize(const uint8_t *data, size_t len) {
        InputEvent ev{};
        if (len >= sizeof(InputEvent)) {
            std::memcpy(&ev, data, sizeof(InputEvent));
        }
        return ev;
    }
};
#pragma pack(pop)

static_assert(sizeof(InputEvent) == 7, "InputEvent size must be 7 bytes");