{
  description = "WebRTC Remote Desktop - cross-platform P2P remote desktop";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          name = "webrtc-remote-desktop-dev";

          buildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            gcc
            SDL2
            ffmpeg
            libdatachannel
            nlohmann_json
            openssl
            libsrtp
            libnice
            p11-kit
          ];

          CMAKE_BUILD_TYPE = "Debug";

          shellHook = ''
            echo "=== WebRTC Remote Desktop Dev Shell ==="
            echo "C++ Compiler: $CXX"
            echo "Run: cmake -B build -G Ninja && cmake --build build"
          '';
        };
      });
}