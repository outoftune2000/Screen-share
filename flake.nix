{
  description = "WebRTC Remote Desktop - high-performance P2P remote desktop";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        buildDeps = with pkgs; [
          cmake
          ninja
          pkg-config
          gcc
        ];

        runtimeDeps = with pkgs; [
          SDL2
          ffmpeg
          libdatachannel
          nlohmann_json
          openssl
          srtp
          libnice
          p11-kit
          avahi
          libuuid
          xorg.libxcb
          xorg.libXfixes
          libva
        ];

        allDeps = buildDeps ++ runtimeDeps;
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "webrtc-remote-desktop";
          version = "1.0.0";

          src = self;

          nativeBuildInputs = buildDeps ++ [ pkgs.wrapGAppsHook ];

          buildInputs = runtimeDeps;

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DWEBRTC_REMOTE_DESKTOP_USE_SYSTEM_LIBDATACHANNEL=ON"
          ];

          installPhase = ''
            runHook preInstall
            install -Dm755 WebRTCRemoteDesktop $out/bin/webrtc-remote-desktop
            install -Dm644 ${./dist/webrtc-remote-desktop.service} $out/share/systemd/user/webrtc-remote-desktop.service
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "High-performance P2P remote desktop using WebRTC";
            platforms = platforms.linux;
          };
        };

        devShells.default = pkgs.mkShell {
          name = "webrtc-remote-desktop-dev";

          buildInputs = allDeps;

          CMAKE_BUILD_TYPE = "Debug";

          shellHook = ''
            echo "=== WebRTC Remote Desktop Dev Shell ==="
            echo ""
            echo "Build:     cmake -B build -G Ninja && cmake --build build"
            echo "Run host:  ./build/WebRTCRemoteDesktop --host"
            echo "Run client:./build/WebRTCRemoteDesktop --client <addr> <port>"
            echo ""
          '';
        };
      });
}