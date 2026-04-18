#ifdef __linux__

#include "uinput_injector.hpp"
#include "keycode_mapping.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <linux/uinput.h>
#include <sys/ioctl.h>

UInputInjector::UInputInjector() = default;

UInputInjector::~UInputInjector() { shutdown(); }

bool UInputInjector::initialize() {
    if (initialized_) {
        return true;
    }

    fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "UInputInjector: failed to open /dev/uinput: "
                  << strerror(errno) << "\n";
        std::cerr << "UInputInjector: ensure udev rules grant access, or run "
                     "with appropriate permissions\n";
        return false;
    }

    ioctl(fd_, UI_SET_EVBIT, EV_KEY);
    ioctl(fd_, UI_SET_EVBIT, EV_REL);
    ioctl(fd_, UI_SET_EVBIT, EV_SYN);

    ioctl(fd_, UI_SET_RELBIT, REL_X);
    ioctl(fd_, UI_SET_RELBIT, REL_Y);
    ioctl(fd_, UI_SET_RELBIT, REL_WHEEL);

    // Mouse buttons
    ioctl(fd_, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd_, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(fd_, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(fd_, UI_SET_KEYBIT, BTN_SIDE);
    ioctl(fd_, UI_SET_KEYBIT, BTN_EXTRA);

    // All keyboard keys from the mapping table
    for (const auto &[generic, linuxKey] : KeyMapping::genericToLinux()) {
        (void)generic;
        ioctl(fd_, UI_SET_KEYBIT, linuxKey);
    }

    // Additional essential keys
    ioctl(fd_, UI_SET_KEYBIT, KEY_LEFTMETA);
    ioctl(fd_, UI_SET_KEYBIT, KEY_RIGHTMETA);

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    usetup.id.version = 1;
    strncpy(usetup.name, "WebRTC_Virtual_Input", UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(fd_, UI_DEV_SETUP, &usetup) < 0) {
        std::cerr << "UInputInjector: UI_DEV_SETUP failed: " << strerror(errno)
                  << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (ioctl(fd_, UI_DEV_CREATE) < 0) {
        std::cerr << "UInputInjector: UI_DEV_CREATE failed: " << strerror(errno)
                  << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    // Give uinput time to create the device
    usleep(100000);

    initialized_ = true;
    std::cout << "UInputInjector: initialized (/dev/uinput)\n";
    return true;
}

void UInputInjector::shutdown() {
    if (initialized_ && fd_ >= 0) {
        ioctl(fd_, UI_DEV_DESTROY);
        close(fd_);
        fd_ = -1;
        initialized_ = false;
        std::cout << "UInputInjector: shut down\n";
    }
}

void UInputInjector::emit(uint16_t type, uint16_t code, int32_t value) {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = value;
    gettimeofday(&ie.time, nullptr);
    write(fd_, &ie, sizeof(ie));
}

void UInputInjector::sync() {
    emit(EV_SYN, SYN_DROPPED, 0);
}

void UInputInjector::inject(const InputEvent &event) {
    if (!initialized_) {
        return;
    }

    switch (event.eventType) {
    case InputEvent::TYPE_MOUSE_MOVE:
        handleMouseMove(event);
        break;
    case InputEvent::TYPE_MOUSE_BUTTON:
        handleMouseButton(event);
        break;
    case InputEvent::TYPE_KEY_PRESS:
        handleKeyPress(event);
        break;
    case InputEvent::TYPE_KEY_RELEASE:
        handleKeyRelease(event);
        break;
    default:
        std::cerr << "UInputInjector: unknown event type "
                  << static_cast<int>(event.eventType) << "\n";
        break;
    }
}

void UInputInjector::handleMouseMove(const InputEvent &event) {
    if (event.x != 0) {
        emit(EV_REL, REL_X, event.x);
    }
    if (event.y != 0) {
        emit(EV_REL, REL_Y, event.y);
    }
    sync();
}

void UInputInjector::handleMouseButton(const InputEvent &event) {
    int btn = BTN_LEFT;
    switch (event.buttonOrKey) {
    case 0: btn = BTN_LEFT; break;
    case 1: btn = BTN_RIGHT; break;
    case 2: btn = BTN_MIDDLE; break;
    case 3: btn = BTN_SIDE; break;
    case 4: btn = BTN_EXTRA; break;
    default: break;
    }

    // Flags bit 0 = pressed, 1 = released
    bool pressed = !(event.flags & 0x01);
    emit(EV_KEY, btn, pressed ? 1 : 0);
    sync();
}

void UInputInjector::handleKeyPress(const InputEvent &event) {
    const auto &map = KeyMapping::genericToLinux();
    auto it = map.find(event.buttonOrKey);
    if (it != map.end()) {
        emit(EV_KEY, it->second, 1);
        sync();
    } else {
        // Fallback: try buttonOrKey as raw Linux key code
        emit(EV_KEY, event.buttonOrKey, 1);
        sync();
    }
}

void UInputInjector::handleKeyRelease(const InputEvent &event) {
    const auto &map = KeyMapping::genericToLinux();
    auto it = map.find(event.buttonOrKey);
    if (it != map.end()) {
        emit(EV_KEY, it->second, 0);
        sync();
    } else {
        emit(EV_KEY, event.buttonOrKey, 0);
        sync();
    }
}

bool UInputInjector::isInitialized() const { return initialized_; }

#endif // __linux__