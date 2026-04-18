#pragma once

#include "screen_capture.hpp"
#include <memory>

std::unique_ptr<ScreenCapture> createScreenCapture();