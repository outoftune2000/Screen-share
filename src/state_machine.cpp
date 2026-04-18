#include "state_machine.hpp"

NodeState StateMachine::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool StateMachine::transitionTo(NodeState newState) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == NodeState::BUSY && newState == NodeState::BUSY) {
        return false;
    }
    state_ = newState;
    return true;
}

bool StateMachine::canAcceptConnection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == NodeState::IDLE;
}

void StateMachine::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = NodeState::IDLE;
}