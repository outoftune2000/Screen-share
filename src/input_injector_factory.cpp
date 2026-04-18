#include "input_injector_factory.hpp"

#ifdef __linux__
#include "uinput_injector.hpp"
#elif defined(_WIN32)
#include "windows_injector.hpp"
#endif

std::unique_ptr<InputInjector> createInputInjector() {
#ifdef __linux__
    return std::make_unique<UInputInjector>();
#elif defined(_WIN32)
    return std::make_unique<WindowsInputInjector>();
#else
    return nullptr;
#endif
}