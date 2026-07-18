cmake_minimum_required(VERSION 3.16)

project(ReplayRecorder)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(X11 REQUIRED)

pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
pkg_check_modules(DBUS REQUIRED dbus-1)
pkg_check_modules(SWSCALE REQUIRED libswscale)


add_executable(replay
    src/main.cpp
    src/App.cpp
    src/AudioCapture.cpp
    src/AudioEncoder.cpp
    src/VideoCapture.cpp
    src/VideoEncoder.cpp
    src/EncoderWorker.cpp
    src/ScreenCast.cpp
    src/PacketBuffer.cpp
    src/ReplayWriter.cpp
    src/Hotkey.cpp
)

target_include_directories(replay PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${DBUS_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
)

target_link_libraries(replay PRIVATE
    ${PIPEWIRE_LIBRARIES}
    ${DBUS_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${X11_LIBRARIES}
    Threads::Threads
)