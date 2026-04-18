#pragma once

#ifdef _WIN32

#include "input_injector.hpp"
#include <windows.h>

class WindowsInputInjector : public InputInjector {
public:
    WindowsInputInjector() = default;
    ~WindowsInputInjector() override = default;

    bool initialize() override;
    void shutdown() override;
    void inject(const InputEvent &event) override;
    bool isInitialized() const override;

private:
    void handleMouseMove(const InputEvent &event);
    void handleMouseButton(const InputEvent &event);
    void handleKeyPress(const InputEvent &event, bool down);

    bool initialized_ = false;
};

#endif // _WIN32