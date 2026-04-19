#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "client_renderer.hpp"
#include "discovery.hpp"
#include "input_injector.hpp"
#include "input_injector_factory.hpp"
#include "input_protocol.hpp"
#include "mdns_broadcaster.hpp"
#include "peer_registry.hpp"
#include "signaling_client.hpp"
#include "signaling_server.hpp"
#include "state_machine.hpp"
#include "uuid_utils.hpp"
#include "video_streamer.hpp"
#include "webrtc_connection.hpp"

#ifdef _WIN32
#include <rtc/rtc.hpp>
#include <libavcodec/avcodec.h>
#else
#include <rtc/rtc.hpp>
#include <libavcodec/avcodec.h>
#endif

static volatile std::sig_atomic_t g_running = 1;

static void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
    }
}

static void printUsage(const char *prog) {
    std::cout << "WebRTC Remote Desktop v1.0\n\n"
              << "Usage:\n"
              << "  " << prog << " --host                    Run as host (share screen, headless)\n"
              << "  " << prog << " --client <addr> <port>    Connect as client\n"
              << "  " << prog << " --client <addr> <port> --fullscreen\n"
              << "  " << prog << "                           Interactive mode (discover & select peer)\n"
              << "  " << prog << " --help                    Show this help\n\n"
              << "Options:\n"
              << "  --host          Host mode: share screen, accept one client\n"
              << "  --client        Client mode: connect to host at addr:port\n"
              << "  --fullscreen    Fullscreen the client window\n"
              << "  --help          Show this help message\n\n"
              << "Interactive mode (no arguments):\n"
              << "  Discovers peers on the LAN via mDNS and presents\n"
              << "  a list to connect to as a client.\n";
}

static std::string nodeStateStr(NodeState s) {
    return nodeStateToString(s);
}

int main(int argc, char *argv[]) {
    // Handle SIGPIPE (ignore - handle at application level)
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "============================================\n";
    std::cout << "  WebRTC Remote Desktop v1.0\n";
    std::cout << "============================================\n";

#ifdef __linux__
    std::cout << "Platform: Linux\n";
#elif defined(_WIN32)
    std::cout << "Platform: Windows\n";
#endif

    std::string instanceId = generateUUID();
    std::string hostname = getHostname();
    std::cout << "Instance ID: " << instanceId << "\n";
    std::cout << "Hostname: " << hostname << "\n";

    enum class RunMode { HOST, CLIENT, INTERACTIVE };
    RunMode mode = RunMode::HOST;
    std::string clientAddr;
    uint16_t clientPort = 0;
    bool fullscreen = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "--host") == 0) {
            mode = RunMode::HOST;
        } else if (std::strcmp(argv[i], "--client") == 0 && i + 2 < argc) {
            mode = RunMode::CLIENT;
            clientAddr = argv[++i];
            clientPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            fullscreen = true;
        }
    }

    // If no --host or --client specified, default to interactive
    bool explicitMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--host") == 0 || std::strcmp(argv[i], "--client") == 0) {
            explicitMode = true;
            break;
        }
    }
    if (!explicitMode && argc > 1) {
        // Has arguments but no mode flag
        std::cerr << "Unknown arguments. Use --help for usage.\n";
        return 1;
    }
    if (argc == 1) {
        mode = RunMode::INTERACTIVE;
    }

    rtc::Preload();

    std::unique_ptr<InputInjector> injector;
    if (mode == RunMode::HOST) {
        injector = createInputInjector();
        if (injector) {
            if (!injector->initialize()) {
                std::cerr << "Warning: Input injection not available (needs /dev/uinput or root)\n";
                injector.reset();
            } else {
                std::cout << "Input injection: initialized\n";
            }
        }
    }

    auto signalingServer = std::make_unique<SignalingServer>();
    auto webrtcConn = std::make_unique<WebRtcConnection>(instanceId,
                        mode == RunMode::HOST ? ConnectionRole::HOST
                                              : ConnectionRole::CLIENT);
    auto signalingClient = std::make_unique<SignalingClient>();
    auto videoStreamer = std::make_unique<VideoStreamer>();
    auto clientRenderer = std::make_unique<ClientRenderer>();
    StateMachine stateMachine;
    PeerRegistry registry;

    webrtcConn->setOnDataChannelOpen([]() {
        std::cout << "*** DataChannel open - input ready ***\n";
    });

    webrtcConn->setOnDataChannelClose([]() {
        std::cout << "*** DataChannel closed ***\n";
    });

    webrtcConn->setOnInputEvent([&injector](const InputEvent &event) {
        if (injector) {
            injector->inject(event);
        }
    });

    webrtcConn->setOnConnectionStateChange(
        [&stateMachine, &videoStreamer, &webrtcConn, mode,
         &clientRenderer, fullscreen](rtc::PeerConnection::State state) {
            if (state == rtc::PeerConnection::State::Connected) {
                std::cout << "WebRTC connected!\n";
                if (mode == RunMode::HOST) {
                    videoStreamer->startCapture(30, 4000000);
                }
            } else if (state == rtc::PeerConnection::State::Disconnected ||
                       state == rtc::PeerConnection::State::Failed ||
                       state == rtc::PeerConnection::State::Closed) {
                stateMachine.transitionTo(NodeState::IDLE);
                std::cout << "Connection lost, state -> IDLE\n";
                videoStreamer->stopCapture();
                clientRenderer->stopEventLoop();
            }
        });

    if (mode == RunMode::HOST) {
        webrtcConn->setOnTrackSetup([&videoStreamer](std::shared_ptr<rtc::PeerConnection> pc) {
            videoStreamer->setupTrack(pc);
            return true;
        });
    }

    if (mode == RunMode::CLIENT) {
        webrtcConn->setOnVideoTrack(
            [&clientRenderer, &webrtcConn, fullscreen](
                std::shared_ptr<rtc::Track> track) {
                std::cout << "DEBUG: Client: received video track, setting up renderer\n";

                track->onFrame([&clientRenderer](rtc::binary data,
                                                     rtc::FrameInfo info) {
                    DecodedFrame frame;
                    frame.data.resize(data.size());
                    std::memcpy(frame.data.data(), data.data(), data.size());
                    frame.width = 0;
                    frame.height = 0;
                    frame.pts = info.timestamp;
                    clientRenderer->onDecodedFrame(frame);
                });

                std::cout << "DEBUG: About to initialize client renderer\n";
                if (clientRenderer->initialize("WebRTC Remote Desktop",
                                                 1280, 720, fullscreen)) {
                    std::cout << "DEBUG: Client renderer initialized successfully\n";
                    clientRenderer->setInputSender(
                        [&webrtcConn](const InputEvent &event) {
                            webrtcConn->sendInputEvent(event);
                        });
                    std::cout << "DEBUG: Starting client renderer thread\n";
                    std::thread([&clientRenderer]() {
                        clientRenderer->runEventLoop();
                    }).detach();
                } else {
                    std::cout << "DEBUG: Client renderer initialization failed\n";
                }
            });
    }

    if (mode == RunMode::HOST) {
        signalingServer->setConnectRequestHandler(
            [&stateMachine](const std::string &peerId) -> bool {
                std::cout << "Connection request from: " << peerId << "\n";
                if (!stateMachine.canAcceptConnection()) {
                    std::cout << "Rejecting (BUSY)\n";
                    return false;
                }
                stateMachine.transitionTo(NodeState::BUSY);
                std::cout << "Accepted, state -> BUSY\n";
                return true;
            });

        signalingServer->setDisconnectHandler(
            [&stateMachine, &videoStreamer](const std::string &peerId) {
                std::cout << "Peer disconnected: " << peerId << "\n";
                stateMachine.transitionTo(NodeState::IDLE);
                videoStreamer->stopCapture();
                std::cout << "State -> IDLE\n";
            });

        if (!signalingServer->start(0)) {
            std::cerr << "Failed to start signaling server\n";
            return 1;
        }

        webrtcConn->initAsHost(*signalingServer);

        std::cout << "Signaling server on port " << signalingServer->port()
                  << "\n";
    }

    // Start mDNS for host and interactive modes
    uint16_t mdnsPort = 0;
    if (mode == RunMode::HOST || mode == RunMode::INTERACTIVE) {
        mdnsPort = signalingServer->port();
    }

    MdnsBroadcaster mdns(instanceId, hostname, mdnsPort);

    mdns.onPeerDiscovered([&registry](const PeerInfo &peer) {
        std::cout << "Peer discovered: " << peer.instanceId << " @"
                  << peer.address << ":" << peer.signalingPort
                  << " state=" << nodeStateToString(peer.state) << "\n";
        registry.addOrUpdate(peer);
    });

    mdns.onPeerRemoved([&registry](const std::string &id) {
        std::cout << "Peer removed: " << id << "\n";
        registry.remove(id);
    });

    if (!mdns.start()) {
        std::cerr << "Failed to start mDNS broadcaster\n";
        return 1;
    }

    // --- Interactive mode: discover peers and let user pick one ---
    if (mode == RunMode::INTERACTIVE) {
        std::cout << "\nDiscovering peers on the LAN (5 seconds)...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto peers = registry.getAll();
        if (peers.empty()) {
            std::cout << "No peers found. Exiting.\n";
            mdns.stop();
            rtc::Cleanup();
            return 0;
        }

        std::cout << "\nAvailable hosts:\n";
        int idx = 1;
        for (const auto &peer : peers) {
            std::cout << "  [" << idx << "] " << peer.hostname
                      << " @ " << peer.address << ":"
                      << peer.signalingPort
                      << " [" << nodeStateToString(peer.state) << "]\n";
            idx++;
        }

        std::cout << "\nSelect a host (1-" << peers.size() << "): ";
        int choice = 0;
        std::cin >> choice;

        if (choice < 1 || choice > static_cast<int>(peers.size())) {
            std::cout << "Invalid selection. Exiting.\n";
            mdns.stop();
            rtc::Cleanup();
            return 1;
        }

        const auto &selected = peers[choice - 1];
        clientAddr = selected.address;
        clientPort = selected.signalingPort;
        mode = RunMode::CLIENT;

        std::cout << "Connecting to " << selected.hostname << " ("
                  << clientAddr << ":" << clientPort << ")...\n";
    }

    // --- Client mode ---
        if (mode == RunMode::CLIENT) {
            std::cout << "DEBUG: Client mode - Connecting to " << clientAddr << ":"
                      << clientPort << "...\n";

            signalingClient->setConnectAcceptHandler([&]() {
                std::cout << "DEBUG: Connected to host, initiating WebRTC...\n";
                webrtcConn->initAsClient(*signalingClient);
            });

            signalingClient->setConnectRejectHandler(
                [](const std::string &reason) {
                    std::cerr << "DEBUG: Connection rejected: " << reason << "\n";
                });

            if (!signalingClient->connect(clientAddr, clientPort, instanceId)) {
                std::cerr << "Failed to connect to signaling server\n";
                mdns.stop();
                rtc::Cleanup();
                return 1;
            }

            int retries = 0;
            while (!signalingClient->isConnected() && g_running && retries < 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                retries++;
            }

            if (!signalingClient->isConnected()) {
                std::cerr << "Timed out connecting to signaling server\n";
            }

            std::cout << "Waiting for video stream... Click inside the window to "
                         "capture mouse.\n";
            std::cout << "Press Escape or close window to release mouse.\n";
        }

    // --- Host mode banner ---
    if (mode == RunMode::HOST) {
        std::cout << "\nHosting on port " << signalingServer->port()
                  << ". Press Ctrl+C to stop.\n\n";
    }

    // --- Main event loop ---
    auto prevState = stateMachine.state();

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto currentState = stateMachine.state();
        if (currentState != prevState) {
            mdns.updateState(currentState);
            prevState = currentState;
        }

        auto now = std::chrono::steady_clock::now();
        auto peers = registry.getAll();
        for (const auto &peer : peers) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                now - peer.lastSeen)
                                .count();
            if (elapsed > 30) {
                registry.remove(peer.instanceId);
            }
        }

        if (mode == RunMode::CLIENT && clientRenderer->isInitialized() &&
            !clientRenderer->isRunning()) {
            std::cout << "Client window closed, exiting...\n";
            break;
        }
    }

    // --- Graceful teardown ---
    std::cout << "\nShutting down...\n";

    // 1. Stop active media
    clientRenderer->stopEventLoop();
    clientRenderer->shutdown();
    videoStreamer->stopCapture();

    // 2. Close WebRTC connection
    webrtcConn->close();

    // 3. Disconnect signaling
    if (signalingClient->isConnected()) {
        signalingClient->disconnect();
    }

    // 4. Release input devices
    if (injector) {
        injector->shutdown();
    }

    // 5. Stop signaling server and mDNS
    signalingServer->stop();
    mdns.stop();

    // 6. Reset state
    stateMachine.transitionTo(NodeState::IDLE);

    // 7. Clean up libdatachannel
    rtc::Cleanup();

    std::cout << "Clean exit.\n";
    return 0;
}