#pragma once

#include "input_protocol.hpp"

class InputInjector {
public:
    virtual ~InputInjector() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void inject(const InputEvent &event) = 0;

    virtual bool isInitialized() const = 0;
};