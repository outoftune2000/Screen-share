#include "screen_capture_factory.hpp"

#ifdef __linux__
#include "xcb_screen_capture.hpp"
#endif

std::unique_ptr<ScreenCapture> createScreenCapture() {
#ifdef __linux__
    return std::make_unique<XcbScreenCapture>();
#else
    return nullptr;
#endif
}