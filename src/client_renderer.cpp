#include "client_renderer.hpp"
#include <iostream>

ClientRenderer::ClientRenderer() = default;

ClientRenderer::~ClientRenderer() { shutdown(); }

bool ClientRenderer::initialize(const std::string &title, int width,
                                  int height, bool fullscreen) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        std::cerr << "ClientRenderer: SDL_Init failed: " << SDL_GetError()
                  << "\n";
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                          SDL_WINDOW_INPUT_FOCUS;
    if (fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, width, height,
                                windowFlags);
    if (!window_) {
        std::cerr << "ClientRenderer: SDL_CreateWindow failed: " << SDL_GetError()
                  << "\n";
        SDL_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
                                    SDL_RENDERER_ACCELERATED |
                                    SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::cerr << "ClientRenderer: SDL_CreateRenderer failed: "
                  << SDL_GetError() << "\n";
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }

    SDL_SetWindowTitle(window_, title.c_str());

    decoder_ = std::make_unique<H264Decoder>();
    if (!decoder_->initialize()) {
        std::cerr << "ClientRenderer: H264 decoder init failed\n";
    }

    initialized_ = true;
    std::cout << "ClientRenderer: initialized (" << width << "x" << height
              << (fullscreen ? " fullscreen" : " windowed") << ")\n";
    return true;
}

void ClientRenderer::shutdown() {
    if (!initialized_) return;

    running_ = false;

    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    if (decoder_) {
        decoder_->shutdown();
    }

    SDL_Quit();
    initialized_ = false;
    std::cout << "ClientRenderer: shut down\n";
}

void ClientRenderer::runEventLoop() {
    running_ = true;
    std::cout << "ClientRenderer: event loop started\n";

    while (running_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handleSdlEvent(event);
        }

        DecodedFrame frame;
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            if (hasNewFrame_) {
                frame = std::move(latestFrame_);
                hasNewFrame_ = false;
            }
        }

        if (!frame.data.empty()) {
            renderFrame(frame);
        } else {
            SDL_Delay(1);
        }
    }

    std::cout << "ClientRenderer: event loop ended\n";
}

void ClientRenderer::stopEventLoop() { running_ = false; }

void ClientRenderer::onDecodedFrame(const DecodedFrame &frame) {
    if (!decoder_ || !decoder_->isInitialized()) {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latestFrame_ = frame;
        hasNewFrame_ = true;
        return;
    }

    auto decoded = decoder_->decode(frame.data.data(), frame.data.size());
    for (auto &df : decoded) {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latestFrame_ = std::move(df);
        hasNewFrame_ = true;
    }
}

void ClientRenderer::setInputSender(
    std::function<void(const InputEvent &)> sender) {
    sendInput_ = std::move(sender);
}

void ClientRenderer::handleSdlEvent(const SDL_Event &event) {
    switch (event.type) {
    case SDL_QUIT:
        running_ = false;
        break;

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        auto ie = sdlEventToInputEvent(event);
        if (sendInput_) sendInput_(ie);
        break;
    }

    case SDL_MOUSEMOTION: {
        if (!mouseTrapped_) break;
        InputEvent ie{};
        ie.eventType = InputEvent::TYPE_MOUSE_MOVE;
        ie.x = static_cast<int16_t>(event.motion.xrel);
        ie.y = static_cast<int16_t>(event.motion.yrel);
        if (sendInput_) sendInput_(ie);
        break;
    }

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        if (!mouseTrapped_) {
            // Trap mouse on click inside window
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (SDL_SetRelativeMouseMode(SDL_TRUE) == 0) {
                    mouseTrapped_ = true;
                    SDL_SetWindowGrab(window_, SDL_TRUE);
                    std::cout << "ClientRenderer: mouse trapped (relative mode)\n";
                }
            }
            break;
        }

        InputEvent ie{};
        ie.eventType = InputEvent::TYPE_MOUSE_BUTTON;
        switch (event.button.button) {
        case SDL_BUTTON_LEFT: ie.buttonOrKey = 0; break;
        case SDL_BUTTON_RIGHT: ie.buttonOrKey = 1; break;
        case SDL_BUTTON_MIDDLE: ie.buttonOrKey = 2; break;
        default: break;
        }
        if (event.type == SDL_MOUSEBUTTONUP) {
            ie.flags |= 0x01; // release flag
        }
        if (sendInput_) sendInput_(ie);
        break;
    }

    case SDL_MOUSEWHEEL: {
        InputEvent ie{};
        ie.eventType = InputEvent::TYPE_MOUSE_MOVE;
        ie.x = static_cast<int16_t>(event.wheel.x);
        ie.y = static_cast<int16_t>(event.wheel.y);
        if (sendInput_) sendInput_(ie);
        break;
    }

    default:
        break;
    }
}

InputEvent ClientRenderer::sdlEventToInputEvent(const SDL_Event &event) {
    InputEvent ie{};
    ie.eventType = (event.type == SDL_KEYDOWN)
                       ? InputEvent::TYPE_KEY_PRESS
                       : InputEvent::TYPE_KEY_RELEASE;

    // SDL keycodes -> generic keycodes
    auto key = event.key.keysym.sym;
    auto mod = event.key.keysym.mod;

    // Modifier flags
    if (mod & KMOD_CTRL) ie.flags |= InputEvent::FLAG_CTRL;
    if (mod & KMOD_ALT) ie.flags |= InputEvent::FLAG_ALT;
    if (mod & KMOD_SHIFT) ie.flags |= InputEvent::FLAG_SHIFT;

    // Map common SDL keys to generic keycodes
    if (key >= SDLK_a && key <= SDLK_z) {
        ie.buttonOrKey = static_cast<uint8_t>(29 + (key - SDLK_a)); // A=42..Z=59
    } else if (key >= SDLK_0 && key <= SDLK_9) {
        ie.buttonOrKey = static_cast<uint8_t>(24 + (key - SDLK_0)); // 0=24..9=33
        if (key == SDLK_0) ie.buttonOrKey = 24;
    } else if (key >= SDLK_F1 && key <= SDLK_F12) {
        ie.buttonOrKey = static_cast<uint8_t>(2 + (key - SDLK_F1));
    } else {
        switch (key) {
        case SDLK_ESCAPE: ie.buttonOrKey = 1; break;
        case SDLK_RETURN: ie.buttonOrKey = 41; break;
        case SDLK_BACKSPACE: ie.buttonOrKey = 27; break;
        case SDLK_TAB: ie.buttonOrKey = 28; break;
        case SDLK_SPACE: ie.buttonOrKey = 64; break;
        case SDLK_LEFT: ie.buttonOrKey = 73; break;
        case SDLK_RIGHT: ie.buttonOrKey = 74; break;
        case SDLK_UP: ie.buttonOrKey = 71; break;
        case SDLK_DOWN: ie.buttonOrKey = 72; break;
        case SDLK_LSHIFT: ie.buttonOrKey = 76; break;
        case SDLK_RSHIFT: ie.buttonOrKey = 77; break;
        case SDLK_LCTRL: ie.buttonOrKey = 78; break;
        case SDLK_RCTRL: ie.buttonOrKey = 79; break;
        case SDLK_LALT: ie.buttonOrKey = 80; break;
        case SDLK_RALT: ie.buttonOrKey = 81; break;
        case SDLK_CAPSLOCK: ie.buttonOrKey = 75; break;
        default: ie.buttonOrKey = static_cast<uint8_t>(key & 0xFF); break;
        }
    }

    return ie;
}

void ClientRenderer::renderFrame(const DecodedFrame &frame) {
    if (frame.data.empty() || frame.width <= 0 || frame.height <= 0) return;

    if (!texture_ || texWidth_ != frame.width || texHeight_ != frame.height) {
        if (texture_) {
            SDL_DestroyTexture(texture_);
        }
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       frame.width, frame.height);
        if (!texture_) {
            std::cerr << "ClientRenderer: SDL_CreateTexture failed: "
                      << SDL_GetError() << "\n";
            return;
        }
        texWidth_ = frame.width;
        texHeight_ = frame.height;
    }

    SDL_UpdateTexture(texture_, nullptr, frame.data.data(),
                      frame.width * 4);

    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}