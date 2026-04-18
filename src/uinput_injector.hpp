#pragma once

#ifdef __linux__

#include "input_injector.hpp"
#include <linux/uinput.h>
#include <cstdint>

class UInputInjector : public InputInjector {
public:
    UInputInjector();
    ~UInputInjector() override;

    bool initialize() override;
    void shutdown() override;
    void inject(const InputEvent &event) override;
    bool isInitialized() const override;

private:
    void emit(uint16_t type, uint16_t code, int32_t value);
    void sync();

    void handleMouseMove(const InputEvent &event);
    void handleMouseButton(const InputEvent &event);
    void handleKeyPress(const InputEvent &event);
    void handleKeyRelease(const InputEvent &event);

    int fd_ = -1;
    bool initialized_ = false;
};

#endif // __linux__