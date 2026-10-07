cmake_minimum_required(VERSION 3.21)
project(dsh-pet-client VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Gui Core Network DBus)

qt_add_executable(dsh-pet-client
    src/main.cpp
    src/petcolor.h
    src/petstate.h       src/petstate.cpp
    src/kwinbridge.h     src/kwinbridge.cpp
    src/tray.h           src/tray.cpp
    src/singleinstance.h src/singleinstance.cpp
    src/autostart.h      src/autostart.cpp
    src/dshlauncher.h    src/dshlauncher.cpp
)

# 鲸鱼像素素材（80x80 BGRA 预乘，与 Windows 版共用同一套 bin）
qt_add_resources(dsh-pet-client "petassets"
    PREFIX "/"
    FILES
        assets/fish_blue.bin
        assets/fish_black.bin
        assets/fish_orange.bin
        assets/fish_green.bin
        assets/dshpet-kwin.js
)

target_link_libraries(dsh-pet-client PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Network Qt6::DBus
)

install(TARGETS dsh-pet-client RUNTIME DESTINATION bin)
