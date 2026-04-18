cmake_minimum_required(VERSION 3.20)
project(WebRTCRemoteDesktop VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WEBRTC_REMOTE_DESKTOP_USE_SYSTEM_LIBDATACHANNEL "Use system-installed libdatachannel instead of fetching" OFF)

find_package(Threads REQUIRED)
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)

if(UNIX)
    pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client)
    pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-shm xfixes)
endif()

if(WEBRTC_REMOTE_DESKTOP_USE_SYSTEM_LIBDATACHANNEL)
    pkg_check_modules(DATACHANNEL REQUIRED IMPORTED_TARGET datachannel)
else()
    include(FetchContent)
    FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG        v0.21.2
    )
    FetchContent_MakeAvailable(libdatachannel)
endif()

set(PROJECT_SOURCES
    src/main.cpp
    src/mdns_broadcaster.cpp
    src/peer_registry.cpp
    src/state_machine.cpp
    src/signaling_server.cpp
    src/signaling_protocol.cpp
    src/signaling_client.cpp
    src/webrtc_connection.cpp
    src/input_injector_factory.cpp
    src/h264_encoder.cpp
    src/h264_decoder.cpp
    src/screen_capture_factory.cpp
    src/video_streamer.cpp
    src/client_renderer.cpp
)

if(UNIX)
    list(APPEND PROJECT_SOURCES
        src/uinput_injector.cpp
        src/xcb_screen_capture.cpp
    )
elseif(WIN32)
    list(APPEND PROJECT_SOURCES src/windows_injector.cpp)
endif()

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<PLATFORM_ID:Windows>:_WIN32>
    $<$<PLATFORM_ID:Linux>:__linux__>
    RTC_ENABLE_WEBSOCKET=1
    RTC_ENABLE_MEDIA=1
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${WEBRTC_REMOTE_DESKTOP_USE_SYSTEM_LIBDATACHANNEL}>:PkgConfig::DATACHANNEL>
    $<$<NOT:$<BOOL:${WEBRTC_REMOTE_DESKTOP_USE_SYSTEM_LIBDATACHANNEL}>>:datachannel>
    PkgConfig::FFMPEG
    SDL2::SDL2
    Threads::Threads
)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dl
        PkgConfig::AVAHI
        PkgConfig::UUID
        PkgConfig::XCB
    )
endif()

# Install
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(FILES dist/webrtc-remote-desktop.service
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/systemd/user
)