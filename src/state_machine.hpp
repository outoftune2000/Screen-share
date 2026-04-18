#pragma once

#include "discovery.hpp"
#include <condition_variable>
#include <mutex>

class StateMachine {
public:
    NodeState state() const;

    bool transitionTo(NodeState newState);

    bool canAcceptConnection() const;

    void reset();

private:
    mutable std::mutex mutex_;
    NodeState state_ = NodeState::IDLE;
};