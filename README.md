# WebRTC Remote Desktop

A high-performance, P2P remote desktop application using WebRTC data channels for input and H.264 for video streaming. Designed for LAN use with zero external dependencies (no STUN/TURN).

## Features

- P2P WebRTC connection with mDNS auto-discovery
- H.264 hardware encoding (VAAPI) with software fallback
- XCB/XShm screen capture (Linux)
- /dev/uinput input injection (Linux), SendInput (Windows)
- Low-latency input via DataChannel (7-byte packed structs)
- WebSocket signaling server for SDP/ICE exchange
- Single-pair mutual exclusion (BUSY/IDLE state machine)

## Building

### Prerequisites (Arch Linux)

```bash
sudo pacman -S cmake sdl2 ffmpeg avahi libuuid
```

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Install

```bash
sudo cmake --install .
```

## Usage

### Host mode (share your screen)

```bash
webrtc-remote-desktop --host
```

### Client mode (connect to a host)

```bash
webrtc-remote-desktop --client 192.168.1.100 8080
webrtc-remote-desktop --client 192.168.1.100 8080 --fullscreen
```

### Interactive mode (discover hosts on LAN)

```bash
webrtc-remote-desktop
```

Lists discovered hosts and prompts for selection.

## Running as a systemd service (Linux)

Install the service file and enable it:

```bash
cp dist/webrtc-remote-desktop.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now webrtc-remote-desktop
```

To view logs:

```bash
journalctl --user -u webrtc-remote-desktop -f
```

## Architecture

```
Host                              Client
+------------------+              +------------------+
| Screen Capture   |              | Client Renderer  |
| (XCB/XShm)       |              | (SDL2)           |
+--------+---------+              +--------+---------+
         |                                 |
+--------v---------+              +--------v---------+
| H264 Encoder     |   WebRTC    | H264 Decoder     |
| (VAAPI/libx264)  +<------------>+(VAAPI/libx264)  |
+------------------+   Video      +------------------+
                             Track
+------------------+              +------------------+
| uinput Injector  |<-------------+ SDL2 Input      |
| (/dev/uinput)    |   DataChan   | Capture          |
+------------------+              +------------------+

         Signaling (WebSocket)
+------------------+              +------------------+
| SignalingServer  |<------------>| SignalingClient  |
| mDNS Broadcaster |              | Peer Discovery   |
+------------------+              +------------------+
```

## CLI Reference

| Flag | Description |
|------|-------------|
| `--host` | Run as host (share screen) |
| `--client <addr> <port>` | Connect to host at address:port |
| `--fullscreen` | Fullscreen client window |
| `--help`, `-h` | Show help message |
| (no args) | Interactive mode: discover and select peer |

## License

Proprietary