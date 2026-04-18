#pragma once

#include "input_injector.hpp"
#include <memory>

std::unique_ptr<InputInjector> createInputInjector();