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

## Installation

### NixOS / Nix

The project includes a flake.nix for both development and building:

```bash
# Enter the development shell
nix develop

# Build the project
nix build

# Run the built binary
./result/bin/webrtc-remote-desktop --host
```

**Permanent installation** - add to your NixOS config or home-manager:

```nix
# flake.nix or configuration.nix
inputs.webrtc-remote-desktop.url = "github:your-org/screenshare";

# In your system config:
environment.systemPackages = [
  inputs.webrtc-remote-desktop.packages.x86_64-linux.default
];
```

Or with home-manager:

```nix
home.packages = [
  inputs.webrtc-remote-desktop.packages.x86_64-linux.default
];
```

**Running as a systemd user service on NixOS:**

```bash
# Copy the service file
cp result/share/systemd/user/webrtc-remote-desktop.service ~/.config/systemd/user/

# Edit ExecStart to point to the nix store path
sed -i "s|/usr/local/bin/webrtc-remote-desktop|$(readlink -f result/bin/webrtc-remote-desktop)|" \
  ~/.config/systemd/user/webrtc-remote-desktop.service

systemctl --user daemon-reload
systemctl --user enable --now webrtc-remote-desktop
```

**udev rules for /dev/uinput (NixOS):**

Add to your `configuration.nix`:

```nix
services.udev.extraRules = ''
  KERNEL=="uinput", MODE="0660", GROUP="input"
'';

# Add your user to the input group
users.users.your-user.extraGroups = [ "input" "video" ];
```

### Arch Linux

```bash
sudo pacman -S cmake sdl2 ffmpeg avahi libuuid libxcb libxfixes libva
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### Other Linux

Install the equivalent dependencies:
- CMake 3.20+, GCC (C++20), SDL2, FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- Avahi (libavahi-client), libuuid, libxcb, libXfixes, libva
- libdatachannel (auto-fetched via CMake FetchContent if not installed)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

## Usage

### Host mode (share your screen)

```bash
# Needs root for /dev/uinput input injection, or udev rules configured
sudo webrtc-remote-desktop --host
```

The host prints its signaling port on startup, e.g.:
```
Signaling server on port 40349
```

### Client mode (connect to a host)

```bash
webrtc-remote-desktop --client 192.168.1.100 40349
webrtc-remote-desktop --client 192.168.1.100 40349 --fullscreen
```

### Interactive mode (discover hosts on LAN)

```bash
webrtc-remote-desktop
```

Discovers hosts via mDNS and prompts you to select one.

## Running as a systemd service (Linux)

```bash
# Install (Arch/other Linux)
sudo cp dist/webrtc-remote-desktop.service /etc/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now webrtc-remote-desktop

# View logs
journalctl --user -u webrtc-remote-desktop -f
```

## CLI Reference

| Flag | Description |
|------|-------------|
| `--host` | Run as host (share screen) |
| `--client <addr> <port>` | Connect to host at address:port |
| `--fullscreen` | Fullscreen client window |
| `--help`, `-h` | Show help message |
| (no args) | Interactive mode: discover and select peer |

## Architecture

```
Host                              Client
+------------------+              +------------------+
| Screen Capture   |              | Client Renderer  |
| (XCB/XShm)       |              | (SDL2)           |
+--------+---------+              +--------+---------+
         |                                 |
+--------v---------+              +--------v---------+
| H264 Encoder      |   WebRTC    | H264 Decoder     |
| (VAAPI/libx264)   +<------------>+ (VAAPI/libx264) |
+------------------+   Video      +------------------+
                              Track
+------------------+              +------------------+
| uinput Injector  |<-------------+ SDL2 Input      |
| (/dev/uinput)    |   DataChan  | Capture          |
+------------------+              +------------------+

         Signaling (WebSocket)
+------------------+              +------------------+
| SignalingServer  |<------------>| SignalingClient  |
| mDNS Broadcaster |              | Peer Discovery   |
+------------------+              +------------------+
```

## NixOS Debug Shell

If you want to build manually inside a Nix shell:

```bash
nix develop
cmake -B build -G Ninja -DWEBRTC_REMOTE_DESKTOP_USE_SYSTEM_LIBDATACHANNEL=ON
cmake --build build
./build/WebRTCRemoteDesktop --host
```

## License

Proprietary