cmake_minimum_required(VERSION 3.16)

project(ReplayRecorder)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
pkg_check_modules(DBUS REQUIRED dbus-1)

find_package(Threads REQUIRED)

add_executable(replay
    src/main.cpp
    src/App.cpp
    src/Capture.cpp
    src/ScreenCast.cpp
)

target_include_directories(replay PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${DBUS_INCLUDE_DIRS}
)

target_link_libraries(replay PRIVATE
    ${PIPEWIRE_LIBRARIES}
    ${DBUS_LIBRARIES}
    Threads::Threads
)