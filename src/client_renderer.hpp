#pragma once

#include "h264_decoder.hpp"
#include "input_protocol.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class ClientRenderer {
public:
    ClientRenderer();
    ~ClientRenderer();

    bool initialize(const std::string &title, int width = 1920,
                    int height = 1080, bool fullscreen = false);
    void shutdown();

    void runEventLoop();
    void stopEventLoop();

    void onDecodedFrame(const DecodedFrame &frame);

    void setInputSender(std::function<void(const InputEvent &)> sender);

    bool isInitialized() const { return initialized_; }
    bool isRunning() const { return running_; }

private:
    void handleSdlEvent(const SDL_Event &event);
    InputEvent sdlEventToInputEvent(const SDL_Event &event);
    void renderFrame(const DecodedFrame &frame);

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    int texWidth_ = 0;
    int texHeight_ = 0;

    std::unique_ptr<H264Decoder> decoder_;
    std::function<void(const InputEvent &)> sendInput_;

    std::mutex frameMutex_;
    DecodedFrame latestFrame_;
    bool hasNewFrame_ = false;

    std::atomic<bool> running_{false};
    std::atomic<bool> mouseTrapped_{false};
    bool initialized_ = false;
};